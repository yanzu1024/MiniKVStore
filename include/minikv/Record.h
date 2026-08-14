#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace minikv {

enum class Operation : std::uint8_t
{
    Set = 1,
    Delete = 2
};

struct Record
{
    Operation operation = Operation::Set;
    std::string key;
    std::string value;
    std::int64_t expiresAtMs = 0;
};

struct RecordMetadata
{
    std::uint64_t recordOffset = 0;
    std::uint64_t valueOffset = 0;
    std::uint32_t valueSize = 0;
    std::int64_t expiresAtMs = 0;
};

inline constexpr std::uint32_t kRecordMagic = 0x31564B4DU; // "MKV1" in little endian.
inline constexpr std::uint16_t kRecordVersion = 1;
inline constexpr std::size_t kRecordHeaderSize = 28;
inline constexpr std::size_t kMaximumKeySize = 4 * 1024;
inline constexpr std::size_t kMaximumValueSize = 16 * 1024 * 1024;

[[nodiscard]] std::uint32_t recordChecksum(
    Operation operation,
    std::int64_t expiresAtMs,
    std::string_view key,
    std::string_view value) noexcept;

[[nodiscard]] std::int64_t unixTimeMilliseconds() noexcept;
[[nodiscard]] bool isExpired(std::int64_t expiresAtMs, std::int64_t nowMs) noexcept;

} // namespace minikv

