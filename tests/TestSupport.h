#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

class TestContext final
{
public:
    void check(bool condition, const std::string& description)
    {
        if (condition) {
            std::cout << "[PASS] " << description << '\n';
        } else {
            std::cerr << "[FAIL] " << description << '\n';
            ++failures_;
        }
    }

    [[nodiscard]] int failures() const noexcept
    {
        return failures_;
    }

private:
    int failures_ = 0;
};

class TempDirectory final
{
public:
    explicit TempDirectory(const std::string& label)
    {
        static std::atomic<unsigned long long> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
                / ("minikv_" + label + "_" + std::to_string(stamp) + "_"
                   + std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }

    ~TempDirectory()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    [[nodiscard]] const std::filesystem::path& path() const noexcept
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void runLruCacheTests(TestContext& context);
void runCommandParserTests(TestContext& context);
void runPersistenceTests(TestContext& context);
void runExpirationTests(TestContext& context);
void runConcurrencyTests(TestContext& context);

