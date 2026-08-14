#include "minikv/Record.h"

#include <chrono>
#include <cstdint>

namespace minikv {
namespace {

void hashByte(std::uint32_t& hash, std::uint8_t byte) noexcept
{
    hash ^= byte;
    hash *= 16777619U;
}

void hash64(std::uint32_t& hash, std::uint64_t value) noexcept
{
    for (unsigned int shift = 0; shift < 64; shift += 8) {
        hashByte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

} // namespace

std::uint32_t recordChecksum(
    Operation operation,
    std::int64_t expiresAtMs,
    std::string_view key,
    std::string_view value) noexcept
{
    std::uint32_t hash = 2166136261U;
    hashByte(hash, static_cast<std::uint8_t>(operation));

    hash64(hash, static_cast<std::uint64_t>(expiresAtMs));
    hash64(hash, static_cast<std::uint64_t>(key.size()));
    hash64(hash, static_cast<std::uint64_t>(value.size()));

    for (const unsigned char character : key) {
        hashByte(hash, character);
    }
    for (const unsigned char character : value) {
        hashByte(hash, character);
    }

    return hash;
}

std::int64_t unixTimeMilliseconds() noexcept
{
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

bool isExpired(std::int64_t expiresAtMs, std::int64_t nowMs) noexcept
{
    return expiresAtMs > 0 && expiresAtMs <= nowMs;
}

} // namespace minikv
