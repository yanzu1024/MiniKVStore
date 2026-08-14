#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <utility>

namespace minikv {

struct CacheStatistics
{
    std::size_t capacity = 0;
    std::size_t size = 0;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
    std::uint64_t evictions = 0;

    [[nodiscard]] double hitRate() const noexcept
    {
        const auto total = hits + misses;
        return total == 0 ? 0.0 : static_cast<double>(hits) / static_cast<double>(total);
    }
};

template<typename Key, typename Value>
class LruCache final
{
public:
    explicit LruCache(std::size_t capacity)
        : capacity_(capacity)
    {
    }

    LruCache(const LruCache&) = delete;
    LruCache& operator=(const LruCache&) = delete;

    [[nodiscard]] std::optional<Value> get(const Key& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = index_.find(key);
        if (found == index_.end()) {
            ++misses_;
            return std::nullopt;
        }

        entries_.splice(entries_.begin(), entries_, found->second);
        ++hits_;
        return found->second->second;
    }

    void put(Key key, Value value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (capacity_ == 0) {
            return;
        }

        const auto found = index_.find(key);
        if (found != index_.end()) {
            found->second->second = std::move(value);
            entries_.splice(entries_.begin(), entries_, found->second);
            return;
        }

        entries_.emplace_front(std::move(key), std::move(value));
        index_[entries_.front().first] = entries_.begin();

        if (entries_.size() > capacity_) {
            const auto& leastRecent = entries_.back();
            index_.erase(leastRecent.first);
            entries_.pop_back();
            ++evictions_;
        }
    }

    void erase(const Key& key)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = index_.find(key);
        if (found == index_.end()) {
            return;
        }
        entries_.erase(found->second);
        index_.erase(found);
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        entries_.clear();
        index_.clear();
    }

    [[nodiscard]] CacheStatistics statistics() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {capacity_, entries_.size(), hits_, misses_, evictions_};
    }

private:
    using EntryList = std::list<std::pair<Key, Value>>;
    using Iterator = typename EntryList::iterator;

    const std::size_t capacity_;
    mutable std::mutex mutex_;
    EntryList entries_;
    std::unordered_map<Key, Iterator> index_;
    std::uint64_t hits_ = 0;
    std::uint64_t misses_ = 0;
    std::uint64_t evictions_ = 0;
};

} // namespace minikv
