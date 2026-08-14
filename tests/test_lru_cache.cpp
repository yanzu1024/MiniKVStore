#include "TestSupport.h"

#include "minikv/LruCache.h"

#include <string>

void runLruCacheTests(TestContext& context)
{
    std::cout << "\n== LRU cache ==\n";
    minikv::LruCache<std::string, std::string> cache(2);
    cache.put("a", "one");
    cache.put("b", "two");

    const auto first = cache.get("a");
    context.check(first.has_value() && *first == "one", "LRU returns a cached value");

    cache.put("c", "three");
    context.check(!cache.get("b").has_value(), "LRU evicts the least recently used entry");
    context.check(cache.get("a").has_value(), "LRU keeps the recently used entry");
    context.check(cache.get("c").has_value(), "LRU keeps the newest entry");

    cache.put("a", "updated");
    const auto updated = cache.get("a");
    context.check(updated.has_value() && *updated == "updated", "LRU updates existing values");

    cache.erase("a");
    context.check(!cache.get("a").has_value(), "LRU erase removes an entry");

    const auto statistics = cache.statistics();
    context.check(statistics.evictions == 1, "LRU eviction statistics are correct");
    context.check(statistics.hits >= 4 && statistics.misses >= 2,
                  "LRU hit and miss statistics are recorded");
}

