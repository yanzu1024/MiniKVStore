#include "minikv/CommandParser.h"
#include "minikv/KeyValueStore.h"
#include "minikv/Status.h"

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {

void printHelp()
{
    std::cout
        << "Commands:\n"
        << "  SET key value                 Store a value\n"
        << "  SET key value TTL seconds     Store a value with expiration\n"
        << "  GET key                       Read a value\n"
        << "  DEL key                       Delete a value\n"
        << "  EXISTS key                    Test whether a key exists\n"
        << "  KEYS [prefix]                 List keys, optionally by prefix\n"
        << "  STATS                         Show engine and cache statistics\n"
        << "  COMPACT                       Rewrite only live records\n"
        << "  HELP                          Show this help\n"
        << "  EXIT                          Close the store and exit\n"
        << "Quote values containing spaces, for example:\n"
        << "  SET greeting \"hello world\" TTL 60\n";
}

void printStatistics(const minikv::EngineStatistics& statistics)
{
    std::cout << "keys                : " << statistics.keyCount << '\n'
              << "data file bytes     : " << statistics.dataFileBytes << '\n'
              << "SET operations      : " << statistics.setOperations << '\n'
              << "GET operations      : " << statistics.getOperations << '\n'
              << "DEL operations      : " << statistics.deleteOperations << '\n'
              << "expired keys        : " << statistics.expiredKeys << '\n'
              << "compactions         : " << statistics.compactions << '\n'
              << "recovered records   : " << statistics.recoveredRecords << '\n'
              << "truncated tail bytes: " << statistics.truncatedTailBytes << '\n'
              << "cache size/capacity : " << statistics.cache.size << '/'
              << statistics.cache.capacity << '\n'
              << "cache hits          : " << statistics.cache.hits << '\n'
              << "cache misses        : " << statistics.cache.misses << '\n'
              << "cache evictions     : " << statistics.cache.evictions << '\n'
              << "cache hit rate      : " << std::fixed << std::setprecision(2)
              << statistics.cache.hitRate() * 100.0 << "%\n";
}

bool parsePositiveSize(std::string_view text, std::size_t& value)
{
    unsigned long long parsed = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()
        || parsed == 0 || parsed > std::numeric_limits<std::size_t>::max()) {
        return false;
    }
    value = static_cast<std::size_t>(parsed);
    return true;
}

void printUsage(const char *program)
{
    std::cout << "Usage: " << program
              << " [--data PATH] [--cache CAPACITY] [--cleanup-ms MILLISECONDS]\n";
}

} // namespace

int main(int argc, char *argv[])
{
    minikv::StoreConfig config;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return EXIT_SUCCESS;
        }
        if (argument == "--data" && index + 1 < argc) {
            config.dataFile = argv[++index];
            continue;
        }
        if (argument == "--cache" && index + 1 < argc) {
            std::size_t capacity = 0;
            if (!parsePositiveSize(argv[++index], capacity)) {
                std::cerr << "Invalid cache capacity.\n";
                return EXIT_FAILURE;
            }
            config.cacheCapacity = capacity;
            continue;
        }
        if (argument == "--cleanup-ms" && index + 1 < argc) {
            std::size_t milliseconds = 0;
            if (!parsePositiveSize(argv[++index], milliseconds)) {
                std::cerr << "Invalid cleanup interval.\n";
                return EXIT_FAILURE;
            }
            config.cleanupInterval = std::chrono::milliseconds(milliseconds);
            continue;
        }

        std::cerr << "Unknown or incomplete argument: " << argument << '\n';
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    minikv::KeyValueStore store(std::move(config));
    const minikv::Status openStatus = store.open();
    if (!openStatus.ok()) {
        std::cerr << "Failed to open MiniKV Store: " << openStatus.toString() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "MiniKV Store 1.0\n"
              << "Persistent C++17 key-value engine. Type HELP for commands.\n";

    std::string line;
    while (std::cout << "minikv> " && std::getline(std::cin, line)) {
        auto commandResult = minikv::CommandParser::parse(line);
        if (!commandResult.ok()) {
            std::cout << "(error) " << commandResult.status().toString() << '\n';
            continue;
        }

        minikv::Command command = commandResult.take();
        switch (command.type) {
        case minikv::CommandType::Set: {
            const minikv::Status status = store.set(
                std::move(command.key), std::move(command.value), command.ttl);
            std::cout << (status.ok() ? "OK" : "(error) " + status.toString()) << '\n';
            break;
        }
        case minikv::CommandType::Get: {
            auto result = store.get(command.key);
            if (result.ok()) {
                std::cout << result.value() << '\n';
            } else if (result.status().code() == minikv::StatusCode::NotFound) {
                std::cout << "(nil)\n";
            } else {
                std::cout << "(error) " << result.status().toString() << '\n';
            }
            break;
        }
        case minikv::CommandType::Delete: {
            const minikv::Status status = store.remove(command.key);
            if (status.ok()) {
                std::cout << "1\n";
            } else if (status.code() == minikv::StatusCode::NotFound) {
                std::cout << "0\n";
            } else {
                std::cout << "(error) " << status.toString() << '\n';
            }
            break;
        }
        case minikv::CommandType::Exists: {
            auto result = store.exists(command.key);
            std::cout << (result.ok() ? (result.value() ? "1" : "0")
                                      : "(error) " + result.status().toString())
                      << '\n';
            break;
        }
        case minikv::CommandType::Keys: {
            auto result = store.keys(command.key);
            if (!result.ok()) {
                std::cout << "(error) " << result.status().toString() << '\n';
                break;
            }
            const auto& keys = result.value();
            for (std::size_t keyIndex = 0; keyIndex < keys.size(); ++keyIndex) {
                std::cout << keyIndex + 1 << ") " << keys[keyIndex] << '\n';
            }
            std::cout << keys.size() << " key(s)\n";
            break;
        }
        case minikv::CommandType::Stats:
            printStatistics(store.statistics());
            break;
        case minikv::CommandType::Compact: {
            auto result = store.compact();
            if (!result.ok()) {
                std::cout << "(error) " << result.status().toString() << '\n';
                break;
            }
            const auto& summary = result.value();
            std::cout << "OK: " << summary.liveKeys << " live key(s), "
                      << summary.oldFileBytes << " -> " << summary.newFileBytes
                      << " bytes\n";
            break;
        }
        case minikv::CommandType::Help:
            printHelp();
            break;
        case minikv::CommandType::Exit:
            static_cast<void>(store.close());
            std::cout << "Bye.\n";
            return EXIT_SUCCESS;
        }
    }

    static_cast<void>(store.close());
    std::cout << '\n';
    return EXIT_SUCCESS;
}
