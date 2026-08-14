#include "minikv/AppendOnlyStore.h"

#include "minikv/Record.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace minikv {
namespace {

struct ParsedHeader
{
    Operation operation = Operation::Set;
    std::uint32_t keySize = 0;
    std::uint32_t valueSize = 0;
    std::int64_t expiresAtMs = 0;
    std::uint32_t checksum = 0;
};

void put16(std::array<char, kRecordHeaderSize>& bytes, std::size_t offset, std::uint16_t value)
{
    for (unsigned int index = 0; index < 2; ++index) {
        bytes[offset + index] = static_cast<char>((value >> (index * 8U)) & 0xFFU);
    }
}

void put32(std::array<char, kRecordHeaderSize>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned int index = 0; index < 4; ++index) {
        bytes[offset + index] = static_cast<char>((value >> (index * 8U)) & 0xFFU);
    }
}

void put64(std::array<char, kRecordHeaderSize>& bytes, std::size_t offset, std::uint64_t value)
{
    for (unsigned int index = 0; index < 8; ++index) {
        bytes[offset + index] = static_cast<char>((value >> (index * 8U)) & 0xFFU);
    }
}

std::uint16_t get16(const std::array<char, kRecordHeaderSize>& bytes, std::size_t offset)
{
    std::uint16_t value = 0;
    for (unsigned int index = 0; index < 2; ++index) {
        value |= static_cast<std::uint16_t>(static_cast<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

std::uint32_t get32(const std::array<char, kRecordHeaderSize>& bytes, std::size_t offset)
{
    std::uint32_t value = 0;
    for (unsigned int index = 0; index < 4; ++index) {
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

std::uint64_t get64(const std::array<char, kRecordHeaderSize>& bytes, std::size_t offset)
{
    std::uint64_t value = 0;
    for (unsigned int index = 0; index < 8; ++index) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset + index]))
                 << (index * 8U);
    }
    return value;
}

std::array<char, kRecordHeaderSize> makeHeader(const Record& record)
{
    std::array<char, kRecordHeaderSize> header{};
    put32(header, 0, kRecordMagic);
    put16(header, 4, kRecordVersion);
    header[6] = static_cast<char>(record.operation);
    header[7] = 0;
    put32(header, 8, static_cast<std::uint32_t>(record.key.size()));
    put32(header, 12, static_cast<std::uint32_t>(record.value.size()));
    put64(header, 16, static_cast<std::uint64_t>(record.expiresAtMs));
    put32(header, 24, recordChecksum(record.operation,
                                    record.expiresAtMs,
                                    record.key,
                                    record.value));
    return header;
}

Result<ParsedHeader> parseHeader(const std::array<char, kRecordHeaderSize>& header)
{
    if (get32(header, 0) != kRecordMagic) {
        return Result<ParsedHeader>::failure(Status::corruption("record magic does not match MKV1"));
    }
    if (get16(header, 4) != kRecordVersion) {
        return Result<ParsedHeader>::failure(Status::corruption("unsupported record version"));
    }

    const auto operationByte = static_cast<std::uint8_t>(header[6]);
    if (operationByte != static_cast<std::uint8_t>(Operation::Set)
        && operationByte != static_cast<std::uint8_t>(Operation::Delete)) {
        return Result<ParsedHeader>::failure(Status::corruption("unknown record operation"));
    }

    ParsedHeader parsed;
    parsed.operation = static_cast<Operation>(operationByte);
    parsed.keySize = get32(header, 8);
    parsed.valueSize = get32(header, 12);
    parsed.expiresAtMs = static_cast<std::int64_t>(get64(header, 16));
    parsed.checksum = get32(header, 24);

    if (parsed.keySize == 0 || parsed.keySize > kMaximumKeySize) {
        return Result<ParsedHeader>::failure(Status::corruption("invalid key size in record"));
    }
    if (parsed.valueSize > kMaximumValueSize) {
        return Result<ParsedHeader>::failure(Status::corruption("invalid value size in record"));
    }
    if (parsed.operation == Operation::Delete && parsed.valueSize != 0) {
        return Result<ParsedHeader>::failure(Status::corruption("delete record contains a value"));
    }

    return Result<ParsedHeader>::success(parsed);
}

Result<RecordMetadata> writeRecord(std::ostream& output,
                                   const Record& record,
                                   std::uint64_t recordOffset)
{
    if (record.key.empty() || record.key.size() > kMaximumKeySize) {
        return Result<RecordMetadata>::failure(Status::invalidArgument("key size is outside the supported range"));
    }
    if (record.value.size() > kMaximumValueSize) {
        return Result<RecordMetadata>::failure(Status::invalidArgument("value exceeds 16 MiB"));
    }
    if (record.operation == Operation::Delete && !record.value.empty()) {
        return Result<RecordMetadata>::failure(Status::invalidArgument("delete record must not contain a value"));
    }

    const auto header = makeHeader(record);
    output.write(header.data(), static_cast<std::streamsize>(header.size()));
    output.write(record.key.data(), static_cast<std::streamsize>(record.key.size()));
    output.write(record.value.data(), static_cast<std::streamsize>(record.value.size()));
    if (!output.good()) {
        return Result<RecordMetadata>::failure(Status::ioError("failed to write append-only record"));
    }

    RecordMetadata metadata;
    metadata.recordOffset = recordOffset;
    metadata.valueOffset = recordOffset + kRecordHeaderSize + record.key.size();
    metadata.valueSize = static_cast<std::uint32_t>(record.value.size());
    metadata.expiresAtMs = record.expiresAtMs;
    return Result<RecordMetadata>::success(metadata);
}

} // namespace

AppendOnlyStore::AppendOnlyStore(std::filesystem::path dataPath)
    : dataPath_(std::move(dataPath))
{
}

AppendOnlyStore::~AppendOnlyStore()
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    if (file_.is_open()) {
        file_.close();
    }
}

Status AppendOnlyStore::open()
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    return ensureOpenLocked();
}

Status AppendOnlyStore::ensureOpenLocked()
{
    if (file_.is_open()) {
        return Status::success();
    }

    std::error_code error;
    const auto parent = dataPath_.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return Status::ioError("failed to create data directory: " + error.message());
        }
    }

    if (!std::filesystem::exists(dataPath_)) {
        std::ofstream createFile(dataPath_, std::ios::binary);
        if (!createFile) {
            return Status::ioError("failed to create data file: " + dataPath_.string());
        }
    }

    file_.open(dataPath_, std::ios::in | std::ios::out | std::ios::binary);
    if (!file_.is_open()) {
        return Status::ioError("failed to open data file: " + dataPath_.string());
    }
    return Status::success();
}

Status AppendOnlyStore::reopenLocked()
{
    if (file_.is_open()) {
        file_.close();
    }
    file_.clear();
    return ensureOpenLocked();
}

Result<RecoveryState> AppendOnlyStore::recover()
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    const Status openStatus = ensureOpenLocked();
    if (!openStatus.ok()) {
        return Result<RecoveryState>::failure(openStatus);
    }

    std::error_code sizeError;
    const std::uint64_t totalBytes = std::filesystem::file_size(dataPath_, sizeError);
    if (sizeError) {
        return Result<RecoveryState>::failure(
            Status::ioError("failed to determine data file size: " + sizeError.message()));
    }

    RecoveryState state;
    std::uint64_t position = 0;
    const std::int64_t nowMs = unixTimeMilliseconds();

    file_.clear();
    file_.seekg(0, std::ios::beg);

    while (position < totalBytes) {
        const std::uint64_t remaining = totalBytes - position;
        if (remaining < kRecordHeaderSize) {
            state.truncatedBytes = remaining;
            break;
        }

        std::array<char, kRecordHeaderSize> headerBytes{};
        file_.read(headerBytes.data(), static_cast<std::streamsize>(headerBytes.size()));
        if (file_.gcount() != static_cast<std::streamsize>(headerBytes.size())) {
            state.truncatedBytes = remaining;
            break;
        }

        auto parsedResult = parseHeader(headerBytes);
        if (!parsedResult.ok()) {
            return Result<RecoveryState>::failure(
                Status::corruption("at byte " + std::to_string(position) + ": "
                                   + parsedResult.status().message()));
        }
        const ParsedHeader header = parsedResult.value();

        const std::uint64_t payloadBytes = static_cast<std::uint64_t>(header.keySize)
                                           + static_cast<std::uint64_t>(header.valueSize);
        const std::uint64_t recordBytes = kRecordHeaderSize + payloadBytes;
        if (recordBytes > remaining) {
            state.truncatedBytes = remaining;
            break;
        }

        std::string key(header.keySize, '\0');
        std::string value(header.valueSize, '\0');
        file_.read(key.data(), static_cast<std::streamsize>(key.size()));
        if (file_.gcount() != static_cast<std::streamsize>(key.size())) {
            return Result<RecoveryState>::failure(
                Status::ioError("failed while reading a record key"));
        }
        file_.read(value.data(), static_cast<std::streamsize>(value.size()));
        if (file_.gcount() != static_cast<std::streamsize>(value.size())) {
            return Result<RecoveryState>::failure(
                Status::ioError("failed while reading a record value"));
        }

        const auto checksum = recordChecksum(header.operation, header.expiresAtMs, key, value);
        if (checksum != header.checksum) {
            return Result<RecoveryState>::failure(
                Status::corruption("checksum mismatch at byte " + std::to_string(position)));
        }

        if (header.operation == Operation::Delete) {
            state.index.erase(key);
        } else if (isExpired(header.expiresAtMs, nowMs)) {
            state.index.erase(key);
        } else {
            state.index[key] = RecordMetadata{
                position,
                position + kRecordHeaderSize + header.keySize,
                header.valueSize,
                header.expiresAtMs
            };
        }

        ++state.validRecords;
        position += recordBytes;
    }

    if (state.truncatedBytes > 0) {
        file_.close();
        std::error_code resizeError;
        std::filesystem::resize_file(dataPath_, position, resizeError);
        if (resizeError) {
            return Result<RecoveryState>::failure(
                Status::ioError("failed to remove incomplete tail: " + resizeError.message()));
        }
        const Status reopenStatus = reopenLocked();
        if (!reopenStatus.ok()) {
            return Result<RecoveryState>::failure(reopenStatus);
        }
    } else {
        file_.clear();
    }

    return Result<RecoveryState>::success(std::move(state));
}

Result<RecordMetadata> AppendOnlyStore::appendRecordLocked(const Record& record)
{
    const Status openStatus = ensureOpenLocked();
    if (!openStatus.ok()) {
        return Result<RecordMetadata>::failure(openStatus);
    }

    file_.clear();
    file_.seekp(0, std::ios::end);
    const auto endPosition = file_.tellp();
    if (endPosition < 0) {
        return Result<RecordMetadata>::failure(Status::ioError("failed to seek to end of data file"));
    }

    const auto offset = static_cast<std::uint64_t>(endPosition);
    auto result = writeRecord(file_, record, offset);
    if (!result.ok()) {
        return result;
    }

    file_.flush();
    if (!file_.good()) {
        return Result<RecordMetadata>::failure(Status::ioError("failed to flush append-only record"));
    }
    return result;
}

Result<RecordMetadata> AppendOnlyStore::appendSet(
    const std::string& key,
    const std::string& value,
    std::int64_t expiresAtMs)
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    return appendRecordLocked(Record{Operation::Set, key, value, expiresAtMs});
}

Status AppendOnlyStore::appendDelete(const std::string& key)
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    auto result = appendRecordLocked(Record{Operation::Delete, key, {}, 0});
    return result.ok() ? Status::success() : result.status();
}

Result<std::string> AppendOnlyStore::readValue(const RecordMetadata& metadata)
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    const Status openStatus = ensureOpenLocked();
    if (!openStatus.ok()) {
        return Result<std::string>::failure(openStatus);
    }

    file_.clear();
    file_.seekg(static_cast<std::streamoff>(metadata.valueOffset), std::ios::beg);
    if (!file_.good()) {
        return Result<std::string>::failure(Status::ioError("failed to seek to value"));
    }

    std::string value(metadata.valueSize, '\0');
    file_.read(value.data(), static_cast<std::streamsize>(value.size()));
    if (file_.gcount() != static_cast<std::streamsize>(value.size())) {
        return Result<std::string>::failure(Status::corruption("value extends beyond the data file"));
    }
    return Result<std::string>::success(std::move(value));
}

Result<CompactionState> AppendOnlyStore::compact(const std::vector<Record>& liveRecords)
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    const Status openStatus = ensureOpenLocked();
    if (!openStatus.ok()) {
        return Result<CompactionState>::failure(openStatus);
    }

    CompactionState state;
    std::error_code sizeError;
    state.oldFileBytes = std::filesystem::file_size(dataPath_, sizeError);
    if (sizeError) {
        return Result<CompactionState>::failure(
            Status::ioError("failed to determine old data file size: " + sizeError.message()));
    }

    const std::filesystem::path temporaryPath = dataPath_.string() + ".compact.tmp";
    std::error_code removeError;
    std::filesystem::remove(temporaryPath, removeError);

    std::ofstream output(temporaryPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        return Result<CompactionState>::failure(Status::ioError("failed to create compaction file"));
    }

    std::uint64_t offset = 0;
    for (const auto& record : liveRecords) {
        auto writeResult = writeRecord(output, record, offset);
        if (!writeResult.ok()) {
            output.close();
            std::filesystem::remove(temporaryPath, removeError);
            return Result<CompactionState>::failure(writeResult.status());
        }
        state.index[record.key] = writeResult.value();
        offset += kRecordHeaderSize + record.key.size() + record.value.size();
    }

    output.flush();
    if (!output.good()) {
        output.close();
        std::filesystem::remove(temporaryPath, removeError);
        return Result<CompactionState>::failure(Status::ioError("failed to flush compaction file"));
    }
    output.close();

    file_.close();
    std::error_code renameError;
    std::filesystem::rename(temporaryPath, dataPath_, renameError);
    if (renameError) {
        const Status reopenStatus = reopenLocked();
        if (!reopenStatus.ok()) {
            return Result<CompactionState>::failure(reopenStatus);
        }
        return Result<CompactionState>::failure(
            Status::ioError("failed to replace data file after compaction: " + renameError.message()));
    }

    const Status reopenStatus = reopenLocked();
    if (!reopenStatus.ok()) {
        return Result<CompactionState>::failure(reopenStatus);
    }

    state.newFileBytes = offset;
    return Result<CompactionState>::success(std::move(state));
}

std::uint64_t AppendOnlyStore::fileSize() const
{
    std::lock_guard<std::mutex> lock(fileMutex_);
    std::error_code error;
    const auto size = std::filesystem::file_size(dataPath_, error);
    return error ? 0 : size;
}

const std::filesystem::path& AppendOnlyStore::dataPath() const noexcept
{
    return dataPath_;
}

} // namespace minikv
