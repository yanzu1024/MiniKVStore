#include "TestSupport.h"

#include "minikv/KeyValueStore.h"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

void runConcurrencyTests(TestContext& context)
{
    std::cout << "\n== Concurrent access ==\n";
    TempDirectory temporary("concurrency");

    minikv::StoreConfig config;
    config.dataFile = temporary.path() / "store.aof";
    config.cacheCapacity = 32;
    config.startBackgroundCleaner = false;

    minikv::KeyValueStore store(config);
    context.check(store.open().ok(), "concurrency test store opens");

    constexpr int threadCount = 6;
    constexpr int keysPerThread = 80;
    std::atomic<int> failures{0};
    std::vector<std::thread> threads;

    for (int threadIndex = 0; threadIndex < threadCount; ++threadIndex) {
        threads.emplace_back([threadIndex, &store, &failures] {
            for (int keyIndex = 0; keyIndex < keysPerThread; ++keyIndex) {
                const std::string key = "thread:" + std::to_string(threadIndex)
                                        + ":key:" + std::to_string(keyIndex);
                const std::string value = "value-" + std::to_string(keyIndex);
                if (!store.set(key, value).ok()) {
                    failures.fetch_add(1);
                    continue;
                }
                auto result = store.get(key);
                if (!result.ok() || result.value() != value) {
                    failures.fetch_add(1);
                }
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }

    context.check(failures.load() == 0, "concurrent SET and GET operations remain consistent");
    auto keys = store.keys("thread:");
    context.check(keys.ok()
                      && keys.value().size() == static_cast<std::size_t>(threadCount * keysPerThread),
                  "all concurrently written keys are indexed");
    static_cast<void>(store.close());

    minikv::KeyValueStore reopened(config);
    context.check(reopened.open().ok(), "concurrent data file reopens");
    auto sample = reopened.get("thread:5:key:79");
    context.check(sample.ok() && sample.value() == "value-79",
                  "concurrently written data survives restart");
    static_cast<void>(reopened.close());
}

