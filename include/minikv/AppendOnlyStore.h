#pragma once

#include "minikv/Record.h"
#include "minikv/Result.h"
#include "minikv/Status.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace minikv {

struct RecoveryState
{
    std::unordered_map<std::string, RecordMetadata> index;
    std::uint64_t validRecords = 0;
    std::uint64_t truncatedBytes = 0;
};

struct CompactionState
{
    std::unordered_map<std::string, RecordMetadata> index;
    std::uint64_t oldFileBytes = 0;
    std::uint64_t newFileBytes = 0;
};

class AppendOnlyStore final
{
public:
    explicit AppendOnlyStore(std::filesystem::path dataPath);
    ~AppendOnlyStore();

    AppendOnlyStore(const AppendOnlyStore&) = delete;
    AppendOnlyStore& operator=(const AppendOnlyStore&) = delete;

    [[nodiscard]] Status open();
    [[nodiscard]] Result<RecoveryState> recover();
    [[nodiscard]] Result<RecordMetadata> appendSet(
        const std::string& key,
        const std::string& value,
        std::int64_t expiresAtMs);
    [[nodiscard]] Status appendDelete(const std::string& key);
    [[nodiscard]] Result<std::string> readValue(const RecordMetadata& metadata);
    [[nodiscard]] Result<CompactionState> compact(const std::vector<Record>& liveRecords);
    [[nodiscard]] std::uint64_t fileSize() const;
    [[nodiscard]] const std::filesystem::path& dataPath() const noexcept;

private:
    [[nodiscard]] Status ensureOpenLocked();
    [[nodiscard]] Result<RecordMetadata> appendRecordLocked(const Record& record);
    [[nodiscard]] Status reopenLocked();

    std::filesystem::path dataPath_;
    mutable std::mutex fileMutex_;
    std::fstream file_;
};

} // namespace minikv

