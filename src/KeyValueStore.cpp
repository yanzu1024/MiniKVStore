#include "minikv/KeyValueStore.h"

#include "minikv/AppendOnlyStore.h"
#include "minikv/Record.h"
#include "minikv/Result.h"
#include "minikv/Status.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <shared_mutex>
#include <string>
#include <utility>
#include <vector>

namespace minikv {

KeyValueStore::KeyValueStore(StoreConfig config)
    : config_(std::move(config))
    , storage_(std::make_unique<AppendOnlyStore>(config_.dataFile))
    , cache_(config_.cacheCapacity)
{
}

KeyValueStore::~KeyValueStore()
{
    static_cast<void>(close());
}

Status KeyValueStore::open()
{
    if (opened_.load(std::memory_order_acquire)) {
        return Status::success();
    }

    if (config_.cleanupInterval.count() <= 0) {
        return Status::invalidArgument("cleanup interval must be greater than zero");
    }

    const Status openStatus = storage_->open();
    if (!openStatus.ok()) {
        return openStatus;
    }

    auto recovery = storage_->recover();
    if (!recovery.ok()) {
        return recovery.status();
    }

    RecoveryState state = recovery.take();
    {
        std::unique_lock<std::shared_mutex> lock(indexMutex_);
        index_ = std::move(state.index);
    }
    cache_.clear();
    recoveredRecords_.store(state.validRecords, std::memory_order_relaxed);
    truncatedTailBytes_.store(state.truncatedBytes, std::memory_order_relaxed);
    stopRequested_.store(false, std::memory_order_release);
    opened_.store(true, std::memory_order_release);

    if (config_.startBackgroundCleaner) {
        try {
            cleanerThread_ = std::thread(&KeyValueStore::cleanerLoop, this);
        } catch (const std::exception& error) {
            opened_.store(false, std::memory_order_release);
            return Status::ioError(std::string("failed to start cleaner thread: ") + error.what());
        }
    }

    return Status::success();
}

Status KeyValueStore::close()
{
    const bool wasOpen = opened_.exchange(false, std::memory_order_acq_rel);
    if (!wasOpen) {
        return Status::success();
    }

    stopRequested_.store(true, std::memory_order_release);
    cleanerCondition_.notify_all();
    if (cleanerThread_.joinable()) {
        cleanerThread_.join();
    }
    return Status::success();
}

Status KeyValueStore::validateKey(std::string_view key) const
{
    if (key.empty()) {
        return Status::invalidArgument("key must not be empty");
    }
    if (key.size() > kMaximumKeySize) {
        return Status::invalidArgument("key exceeds 4 KiB");
    }
    return Status::success();
}

Status KeyValueStore::validateValue(std::string_view value) const
{
    if (value.size() > kMaximumValueSize) {
        return Status::invalidArgument("value exceeds 16 MiB");
    }
    return Status::success();
}

Status KeyValueStore::set(
    std::string key,
    std::string value,
    std::optional<std::chrono::milliseconds> ttl)
{
    if (!isOpen()) {
        return Status::closed("store is not open");
    }

    const Status keyStatus = validateKey(key);
    if (!keyStatus.ok()) {
        return keyStatus;
    }
    const Status valueStatus = validateValue(value);
    if (!valueStatus.ok()) {
        return valueStatus;
    }
    if (ttl.has_value() && ttl->count() <= 0) {
        return Status::invalidArgument("TTL must be greater than zero");
    }

    const std::int64_t expiresAtMs = ttl.has_value()
        ? unixTimeMilliseconds() + ttl->count()
        : 0;

    std::unique_lock<std::shared_mutex> lock(indexMutex_);
    if (!isOpen()) {
        return Status::closed("store was closed during SET");
    }

    auto appendResult = storage_->appendSet(key, value, expiresAtMs);
    if (!appendResult.ok()) {
        return appendResult.status();
    }

    index_[key] = appendResult.value();
    cache_.put(key, value);
    setOperations_.fetch_add(1, std::memory_order_relaxed);
    return Status::success();
}

Result<std::string> KeyValueStore::get(const std::string& key)
{
    if (!isOpen()) {
        return Result<std::string>::failure(Status::closed("store is not open"));
    }
    const Status keyStatus = validateKey(key);
    if (!keyStatus.ok()) {
        return Result<std::string>::failure(keyStatus);
    }

    getOperations_.fetch_add(1, std::memory_order_relaxed);
    std::shared_lock<std::shared_mutex> lock(indexMutex_);
    const auto found = index_.find(key);
    if (found == index_.end()) {
        return Result<std::string>::failure(Status::notFound("key does not exist"));
    }

    const std::int64_t nowMs = unixTimeMilliseconds();
    if (isExpired(found->second.expiresAtMs, nowMs)) {
        lock.unlock();
        static_cast<void>(eraseIfExpired(key, nowMs));
        return Result<std::string>::failure(Status::notFound("key has expired"));
    }

    if (auto cached = cache_.get(key); cached.has_value()) {
        return Result<std::string>::success(std::move(*cached));
    }

    auto readResult = storage_->readValue(found->second);
    if (!readResult.ok()) {
        return readResult;
    }

    std::string value = readResult.take();
    cache_.put(key, value);
    return Result<std::string>::success(std::move(value));
}

bool KeyValueStore::eraseIfExpired(const std::string& key, std::int64_t nowMs)
{
    std::unique_lock<std::shared_mutex> lock(indexMutex_);
    const auto found = index_.find(key);
    if (found == index_.end() || !isExpired(found->second.expiresAtMs, nowMs)) {
        return false;
    }

    cache_.erase(key);
    index_.erase(found);
    expiredKeys_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

Status KeyValueStore::remove(const std::string& key)
{
    if (!isOpen()) {
        return Status::closed("store is not open");
    }
    const Status keyStatus = validateKey(key);
    if (!keyStatus.ok()) {
        return keyStatus;
    }

    std::unique_lock<std::shared_mutex> lock(indexMutex_);
    const auto found = index_.find(key);
    if (found == index_.end()) {
        return Status::notFound("key does not exist");
    }
    if (isExpired(found->second.expiresAtMs, unixTimeMilliseconds())) {
        cache_.erase(key);
        index_.erase(found);
        expiredKeys_.fetch_add(1, std::memory_order_relaxed);
        return Status::notFound("key has expired");
    }

    const Status appendStatus = storage_->appendDelete(key);
    if (!appendStatus.ok()) {
        return appendStatus;
    }

    cache_.erase(key);
    index_.erase(found);
    deleteOperations_.fetch_add(1, std::memory_order_relaxed);
    return Status::success();
}

Result<bool> KeyValueStore::exists(const std::string& key)
{
    auto result = get(key);
    if (result.ok()) {
        return Result<bool>::success(true);
    }
    if (result.status().code() == StatusCode::NotFound) {
        return Result<bool>::success(false);
    }
    return Result<bool>::failure(result.status());
}

Result<std::vector<std::string>> KeyValueStore::keys(std::string_view prefix)
{
    if (!isOpen()) {
        return Result<std::vector<std::string>>::failure(Status::closed("store is not open"));
    }

    static_cast<void>(cleanupExpired());
    std::shared_lock<std::shared_mutex> lock(indexMutex_);
    std::vector<std::string> result;
    result.reserve(index_.size());

    for (const auto& [key, metadata] : index_) {
        static_cast<void>(metadata);
        if (prefix.empty() || key.compare(0, prefix.size(), prefix) == 0) {
            result.push_back(key);
        }
    }
    std::sort(result.begin(), result.end());
    return Result<std::vector<std::string>>::success(std::move(result));
}

Result<CompactionSummary> KeyValueStore::compact()
{
    if (!isOpen()) {
        return Result<CompactionSummary>::failure(Status::closed("store is not open"));
    }

    std::unique_lock<std::shared_mutex> lock(indexMutex_);
    const std::int64_t nowMs = unixTimeMilliseconds();
    for (auto iterator = index_.begin(); iterator != index_.end();) {
        if (isExpired(iterator->second.expiresAtMs, nowMs)) {
            cache_.erase(iterator->first);
            iterator = index_.erase(iterator);
            expiredKeys_.fetch_add(1, std::memory_order_relaxed);
        } else {
            ++iterator;
        }
    }

    std::vector<Record> liveRecords;
    liveRecords.reserve(index_.size());
    for (const auto& [key, metadata] : index_) {
        auto readResult = storage_->readValue(metadata);
        if (!readResult.ok()) {
            return Result<CompactionSummary>::failure(readResult.status());
        }
        liveRecords.push_back(Record{Operation::Set,
                                     key,
                                     readResult.take(),
                                     metadata.expiresAtMs});
    }

    std::sort(liveRecords.begin(), liveRecords.end(), [](const Record& left, const Record& right) {
        return left.key < right.key;
    });

    auto compactResult = storage_->compact(liveRecords);
    if (!compactResult.ok()) {
        return Result<CompactionSummary>::failure(compactResult.status());
    }

    CompactionState state = compactResult.take();
    index_ = std::move(state.index);
    cache_.clear();
    compactions_.fetch_add(1, std::memory_order_relaxed);

    return Result<CompactionSummary>::success(
        CompactionSummary{index_.size(), state.oldFileBytes, state.newFileBytes});
}

EngineStatistics KeyValueStore::statistics() const
{
    EngineStatistics result;
    {
        std::shared_lock<std::shared_mutex> lock(indexMutex_);
        result.keyCount = index_.size();
    }
    result.dataFileBytes = storage_->fileSize();
    result.getOperations = getOperations_.load(std::memory_order_relaxed);
    result.setOperations = setOperations_.load(std::memory_order_relaxed);
    result.deleteOperations = deleteOperations_.load(std::memory_order_relaxed);
    result.expiredKeys = expiredKeys_.load(std::memory_order_relaxed);
    result.compactions = compactions_.load(std::memory_order_relaxed);
    result.recoveredRecords = recoveredRecords_.load(std::memory_order_relaxed);
    result.truncatedTailBytes = truncatedTailBytes_.load(std::memory_order_relaxed);
    result.cache = cache_.statistics();
    return result;
}

std::size_t KeyValueStore::cleanupExpired()
{
    if (!isOpen()) {
        return 0;
    }

    std::unique_lock<std::shared_mutex> lock(indexMutex_);
    const std::int64_t nowMs = unixTimeMilliseconds();
    std::size_t removed = 0;
    for (auto iterator = index_.begin(); iterator != index_.end();) {
        if (isExpired(iterator->second.expiresAtMs, nowMs)) {
            cache_.erase(iterator->first);
            iterator = index_.erase(iterator);
            ++removed;
        } else {
            ++iterator;
        }
    }
    expiredKeys_.fetch_add(removed, std::memory_order_relaxed);
    return removed;
}

bool KeyValueStore::isOpen() const noexcept
{
    return opened_.load(std::memory_order_acquire);
}

void KeyValueStore::cleanerLoop()
{
    std::unique_lock<std::mutex> lock(cleanerMutex_);
    while (!stopRequested_.load(std::memory_order_acquire)) {
        cleanerCondition_.wait_for(lock, config_.cleanupInterval, [this] {
            return stopRequested_.load(std::memory_order_acquire);
        });

        if (stopRequested_.load(std::memory_order_acquire)) {
            break;
        }

        lock.unlock();
        static_cast<void>(cleanupExpired());
        lock.lock();
    }
}

} // namespace minikv
