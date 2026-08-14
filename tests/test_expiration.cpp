#include "TestSupport.h"

#include "minikv/KeyValueStore.h"
#include "minikv/Status.h"

#include <chrono>
#include <thread>

void runExpirationTests(TestContext& context)
{
    std::cout << "\n== TTL expiration ==\n";
    TempDirectory temporary("expiration");

    minikv::StoreConfig config;
    config.dataFile = temporary.path() / "store.aof";
    config.cacheCapacity = 4;
    config.cleanupInterval = std::chrono::milliseconds(10);
    config.startBackgroundCleaner = true;

    minikv::KeyValueStore store(config);
    context.check(store.open().ok(), "TTL test store opens");
    context.check(store.set("short", "lived", std::chrono::milliseconds(40)).ok(),
                  "value with TTL is stored");

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    auto result = store.get("short");
    context.check(!result.ok() && result.status().code() == minikv::StatusCode::NotFound,
                  "expired value is not returned");
    context.check(store.statistics().keyCount == 0,
                  "background cleaner removes expired metadata");
    context.check(store.statistics().expiredKeys >= 1,
                  "expiration statistics are updated");
    static_cast<void>(store.close());

    minikv::KeyValueStore reopened(config);
    context.check(reopened.open().ok(), "store containing expired record reopens");
    context.check(!reopened.get("short").ok(), "expired record is ignored during recovery");
    static_cast<void>(reopened.close());
}

