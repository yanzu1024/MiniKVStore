#pragma once

#include "minikv/Result.h"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace minikv {

enum class CommandType
{
    Set,
    Get,
    Delete,
    Exists,
    Keys,
    Stats,
    Compact,
    Help,
    Exit
};

struct Command
{
    CommandType type = CommandType::Help;
    std::string key;
    std::string value;
    std::optional<std::chrono::milliseconds> ttl;
};

class CommandParser final
{
public:
    [[nodiscard]] static Result<Command> parse(std::string_view input);
    [[nodiscard]] static Result<std::vector<std::string>> tokenize(std::string_view input);
};

} // namespace minikv

