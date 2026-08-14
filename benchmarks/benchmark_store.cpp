#include "minikv/KeyValueStore.h"

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <system_error>

namespace {

double operationsPerSecond(std::size_t operations, std::chrono::steady_clock::duration duration)
{
    const double seconds = std::chrono::duration<double>(duration).count();
    return seconds == 0.0 ? 0.0 : static_cast<double>(operations) / seconds;
}

} // namespace

int main(int argc, char *argv[])
{
    std::size_t operationCount = 20000;
    if (argc == 2) {
        try {
            operationCount = static_cast<std::size_t>(std::stoull(argv[1]));
        } catch (const std::exception&) {
            std::cerr << "Usage: " << argv[0] << " [positive-operation-count]\n";
            return EXIT_FAILURE;
        }
    }
    if (operationCount == 0) {
        std::cerr << "Operation count must be positive.\n";
        return EXIT_FAILURE;
    }

    const auto temporaryDirectory = std::filesystem::temp_directory_path()
                                    / "minikv_benchmark_data";
    std::error_code error;
    std::filesystem::remove_all(temporaryDirectory, error);
    std::filesystem::create_directories(temporaryDirectory);

    minikv::StoreConfig config;
    config.dataFile = temporaryDirectory / "benchmark.aof";
    config.cacheCapacity = 1024;
    config.startBackgroundCleaner = false;

    minikv::KeyValueStore store(config);
    const auto openStatus = store.open();
    if (!openStatus.ok()) {
        std::cerr << openStatus.toString() << '\n';
        return EXIT_FAILURE;
    }

    const auto writeStart = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < operationCount; ++index) {
        const auto status = store.set("key:" + std::to_string(index),
                                      "value:" + std::to_string(index));
        if (!status.ok()) {
            std::cerr << status.toString() << '\n';
            return EXIT_FAILURE;
        }
    }
    const auto writeEnd = std::chrono::steady_clock::now();

    const auto readStart = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < operationCount; ++index) {
        auto result = store.get("key:" + std::to_string(index));
        if (!result.ok()) {
            std::cerr << result.status().toString() << '\n';
            return EXIT_FAILURE;
        }
    }
    const auto readEnd = std::chrono::steady_clock::now();

    auto compactResult = store.compact();
    if (!compactResult.ok()) {
        std::cerr << compactResult.status().toString() << '\n';
        return EXIT_FAILURE;
    }

    std::cout << std::fixed << std::setprecision(0)
              << "operations        : " << operationCount << '\n'
              << "SET operations/s  : "
              << operationsPerSecond(operationCount, writeEnd - writeStart) << '\n'
              << "GET operations/s  : "
              << operationsPerSecond(operationCount, readEnd - readStart) << '\n'
              << "file after compact: " << compactResult.value().newFileBytes << " bytes\n"
              << "cache hit rate    : " << std::setprecision(2)
              << store.statistics().cache.hitRate() * 100.0 << "%\n";

    static_cast<void>(store.close());
    std::filesystem::remove_all(temporaryDirectory, error);
    return EXIT_SUCCESS;
}
