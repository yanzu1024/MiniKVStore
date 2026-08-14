#pragma once

#include "minikv/AppendOnlyStore.h"
#include "minikv/LruCache.h"
#include "minikv/Result.h"
#include "minikv/Status.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace minikv {

struct StoreConfig
{
    std::filesystem::path dataFile = "data/minikv.aof";
    std::size_t cacheCapacity = 128;
    std::chrono::milliseconds cleanupInterval{1000};
    bool startBackgroundCleaner = true;
};

struct EngineStatistics
{
    std::size_t keyCount = 0;
    std::uint64_t dataFileBytes = 0;
    std::uint64_t getOperations = 0;
    std::uint64_t setOperations = 0;
    std::uint64_t deleteOperations = 0;
    std::uint64_t expiredKeys = 0;
    std::uint64_t compactions = 0;
    std::uint64_t recoveredRecords = 0;
    std::uint64_t truncatedTailBytes = 0;
    CacheStatistics cache;
};

struct CompactionSummary
{
    std::size_t liveKeys = 0;
    std::uint64_t oldFileBytes = 0;
    std::uint64_t newFileBytes = 0;
};

class KeyValueStore final
{
public:
    explicit KeyValueStore(StoreConfig config = {});
    ~KeyValueStore();

    KeyValueStore(const KeyValueStore&) = delete;
    KeyValueStore& operator=(const KeyValueStore&) = delete;
    KeyValueStore(KeyValueStore&&) = delete;
    KeyValueStore& operator=(KeyValueStore&&) = delete;

    [[nodiscard]] Status open();
    [[nodiscard]] Status close();

    [[nodiscard]] Status set(
        std::string key,
        std::string value,
        std::optional<std::chrono::milliseconds> ttl = std::nullopt);
    [[nodiscard]] Result<std::string> get(const std::string& key);
    [[nodiscard]] Status remove(const std::string& key);
    [[nodiscard]] Result<bool> exists(const std::string& key);
    [[nodiscard]] Result<std::vector<std::string>> keys(std::string_view prefix = {});
    [[nodiscard]] Result<CompactionSummary> compact();
    [[nodiscard]] EngineStatistics statistics() const;
    [[nodiscard]] std::size_t cleanupExpired();
    [[nodiscard]] bool isOpen() const noexcept;

private:
    [[nodiscard]] Status validateKey(std::string_view key) const;
    [[nodiscard]] Status validateValue(std::string_view value) const;
    [[nodiscard]] bool eraseIfExpired(const std::string& key, std::int64_t nowMs);
    void cleanerLoop();

    StoreConfig config_;
    std::unique_ptr<AppendOnlyStore> storage_;
    LruCache<std::string, std::string> cache_;

    mutable std::shared_mutex indexMutex_;
    std::unordered_map<std::string, RecordMetadata> index_;

    std::atomic_bool opened_{false};
    std::atomic_bool stopRequested_{false};
    std::thread cleanerThread_;
    std::mutex cleanerMutex_;
    std::condition_variable cleanerCondition_;

    std::atomic<std::uint64_t> getOperations_{0};
    std::atomic<std::uint64_t> setOperations_{0};
    std::atomic<std::uint64_t> deleteOperations_{0};
    std::atomic<std::uint64_t> expiredKeys_{0};
    std::atomic<std::uint64_t> compactions_{0};
    std::atomic<std::uint64_t> recoveredRecords_{0};
    std::atomic<std::uint64_t> truncatedTailBytes_{0};
};

} // namespace minikv

