#include "TestSupport.h"

#include "minikv/KeyValueStore.h"
#include "minikv/Status.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace {

minikv::StoreConfig testConfig(const std::filesystem::path& file)
{
    minikv::StoreConfig config;
    config.dataFile = file;
    config.cacheCapacity = 2;
    config.startBackgroundCleaner = false;
    return config;
}

} // namespace

void runPersistenceTests(TestContext& context)
{
    std::cout << "\n== Persistence and compaction ==\n";
    TempDirectory temporary("persistence");
    const auto dataFile = temporary.path() / "store.aof";

    {
        minikv::KeyValueStore store(testConfig(dataFile));
        context.check(store.open().ok(), "new store opens");
        context.check(store.set("alpha", "one").ok(), "first value is stored");
        context.check(store.set("alpha", "two").ok(), "existing key is updated");
        context.check(store.set("beta", "temporary").ok(), "second key is stored");
        context.check(store.remove("beta").ok(), "key is deleted with a tombstone");
        context.check(store.close().ok(), "store closes cleanly");
    }

    const auto bytesBeforeCompaction = std::filesystem::file_size(dataFile);
    {
        minikv::KeyValueStore store(testConfig(dataFile));
        context.check(store.open().ok(), "existing store reopens");

        auto alpha = store.get("alpha");
        context.check(alpha.ok() && alpha.value() == "two", "latest value survives restart");
        auto beta = store.get("beta");
        context.check(!beta.ok() && beta.status().code() == minikv::StatusCode::NotFound,
                      "deleted key remains deleted after restart");
        context.check(store.statistics().recoveredRecords == 4,
                      "startup recovery scans every valid record");

        auto compactResult = store.compact();
        context.check(compactResult.ok(), "compaction succeeds");
        context.check(compactResult.ok() && compactResult.value().liveKeys == 1,
                      "compaction retains only live keys");
        context.check(compactResult.ok()
                          && compactResult.value().newFileBytes < bytesBeforeCompaction,
                      "compaction reduces the append-only file");
        context.check(store.close().ok(), "compacted store closes");
    }

    {
        minikv::KeyValueStore store(testConfig(dataFile));
        context.check(store.open().ok(), "compacted store reopens");
        auto alpha = store.get("alpha");
        context.check(alpha.ok() && alpha.value() == "two",
                      "value remains correct after compaction and restart");
        static_cast<void>(store.close());
    }

    std::cout << "\n== Truncated tail recovery ==\n";
    TempDirectory truncatedTemporary("truncated");
    const auto truncatedFile = truncatedTemporary.path() / "store.aof";
    std::uint64_t validBytes = 0;
    {
        minikv::KeyValueStore store(testConfig(truncatedFile));
        context.check(store.open().ok(), "truncation test store opens");
        context.check(store.set("safe", "value").ok(), "valid record is written");
        static_cast<void>(store.close());
        validBytes = std::filesystem::file_size(truncatedFile);
    }
    {
        std::ofstream output(truncatedFile, std::ios::binary | std::ios::app);
        output.write("BAD!!", 5);
    }
    {
        minikv::KeyValueStore store(testConfig(truncatedFile));
        context.check(store.open().ok(), "store removes an incomplete tail during recovery");
        context.check(store.statistics().truncatedTailBytes == 5,
                      "recovery reports the truncated byte count");
        auto value = store.get("safe");
        context.check(value.ok() && value.value() == "value",
                      "valid records before the truncated tail are preserved");
        context.check(std::filesystem::file_size(truncatedFile) == validBytes,
                      "data file is resized to its last valid record");
        static_cast<void>(store.close());
    }

    std::cout << "\n== Checksum corruption detection ==\n";
    TempDirectory corruptedTemporary("corrupted");
    const auto corruptedFile = corruptedTemporary.path() / "store.aof";
    {
        minikv::KeyValueStore store(testConfig(corruptedFile));
        context.check(store.open().ok(), "corruption test store opens");
        context.check(store.set("checked", "payload").ok(), "checksummed record is written");
        static_cast<void>(store.close());
    }
    {
        std::fstream file(corruptedFile, std::ios::in | std::ios::out | std::ios::binary);
        file.seekg(-1, std::ios::end);
        char byte = 0;
        file.read(&byte, 1);
        byte ^= 0x01;
        file.seekp(-1, std::ios::end);
        file.write(&byte, 1);
    }
    {
        minikv::KeyValueStore store(testConfig(corruptedFile));
        const auto status = store.open();
        context.check(!status.ok() && status.code() == minikv::StatusCode::Corruption,
                      "checksum mismatch is reported instead of silently truncated");
    }
}
