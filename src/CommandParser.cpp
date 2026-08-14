#include "minikv/CommandParser.h"

#include "minikv/Result.h"
#include "minikv/Status.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace minikv {
namespace {

std::string uppercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    return text;
}

Result<std::chrono::milliseconds> parseTtl(std::string_view text)
{
    std::int64_t seconds = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), seconds);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || seconds <= 0) {
        return Result<std::chrono::milliseconds>::failure(
            Status::invalidArgument("TTL must be a positive integer number of seconds"));
    }
    if (seconds > std::numeric_limits<std::int64_t>::max() / 1000) {
        return Result<std::chrono::milliseconds>::failure(Status::invalidArgument("TTL is too large"));
    }
    return Result<std::chrono::milliseconds>::success(std::chrono::milliseconds(seconds * 1000));
}

Status requireSize(const std::vector<std::string>& tokens,
                   std::size_t expected,
                   std::string_view usage)
{
    if (tokens.size() != expected) {
        return Status::invalidArgument("usage: " + std::string(usage));
    }
    return Status::success();
}

} // namespace

Result<std::vector<std::string>> CommandParser::tokenize(std::string_view input)
{
    std::vector<std::string> tokens;
    std::string current;
    bool inQuotes = false;
    bool escaping = false;
    bool tokenStarted = false;

    for (const char character : input) {
        if (escaping) {
            switch (character) {
            case 'n':
                current.push_back('\n');
                break;
            case 't':
                current.push_back('\t');
                break;
            case '"':
            case '\\':
                current.push_back(character);
                break;
            default:
                return Result<std::vector<std::string>>::failure(
                    Status::invalidArgument("unsupported escape sequence"));
            }
            escaping = false;
            tokenStarted = true;
            continue;
        }

        if (inQuotes && character == '\\') {
            escaping = true;
            continue;
        }
        if (character == '"') {
            inQuotes = !inQuotes;
            tokenStarted = true;
            continue;
        }
        if (!inQuotes && std::isspace(static_cast<unsigned char>(character)) != 0) {
            if (tokenStarted) {
                tokens.push_back(std::move(current));
                current.clear();
                tokenStarted = false;
            }
            continue;
        }

        current.push_back(character);
        tokenStarted = true;
    }

    if (escaping) {
        return Result<std::vector<std::string>>::failure(
            Status::invalidArgument("unfinished escape sequence"));
    }
    if (inQuotes) {
        return Result<std::vector<std::string>>::failure(
            Status::invalidArgument("missing closing quote"));
    }
    if (tokenStarted) {
        tokens.push_back(std::move(current));
    }

    return Result<std::vector<std::string>>::success(std::move(tokens));
}

Result<Command> CommandParser::parse(std::string_view input)
{
    auto tokenResult = tokenize(input);
    if (!tokenResult.ok()) {
        return Result<Command>::failure(tokenResult.status());
    }

    std::vector<std::string> tokens = tokenResult.take();
    if (tokens.empty()) {
        return Result<Command>::failure(Status::invalidArgument("command is empty"));
    }

    const std::string name = uppercase(tokens.front());
    Command command;

    if (name == "SET") {
        if (tokens.size() != 3 && tokens.size() != 5) {
            return Result<Command>::failure(
                Status::invalidArgument("usage: SET key value [TTL seconds]"));
        }
        command.type = CommandType::Set;
        command.key = std::move(tokens[1]);
        command.value = std::move(tokens[2]);
        if (tokens.size() == 5) {
            if (uppercase(tokens[3]) != "TTL") {
                return Result<Command>::failure(
                    Status::invalidArgument("usage: SET key value [TTL seconds]"));
            }
            auto ttlResult = parseTtl(tokens[4]);
            if (!ttlResult.ok()) {
                return Result<Command>::failure(ttlResult.status());
            }
            command.ttl = ttlResult.take();
        }
        return Result<Command>::success(std::move(command));
    }

    if (name == "GET") {
        const Status sizeStatus = requireSize(tokens, 2, "GET key");
        if (!sizeStatus.ok()) {
            return Result<Command>::failure(sizeStatus);
        }
        command.type = CommandType::Get;
        command.key = std::move(tokens[1]);
        return Result<Command>::success(std::move(command));
    }

    if (name == "DEL" || name == "DELETE") {
        const Status sizeStatus = requireSize(tokens, 2, "DEL key");
        if (!sizeStatus.ok()) {
            return Result<Command>::failure(sizeStatus);
        }
        command.type = CommandType::Delete;
        command.key = std::move(tokens[1]);
        return Result<Command>::success(std::move(command));
    }

    if (name == "EXISTS") {
        const Status sizeStatus = requireSize(tokens, 2, "EXISTS key");
        if (!sizeStatus.ok()) {
            return Result<Command>::failure(sizeStatus);
        }
        command.type = CommandType::Exists;
        command.key = std::move(tokens[1]);
        return Result<Command>::success(std::move(command));
    }

    if (name == "KEYS") {
        if (tokens.size() > 2) {
            return Result<Command>::failure(Status::invalidArgument("usage: KEYS [prefix]"));
        }
        command.type = CommandType::Keys;
        if (tokens.size() == 2) {
            command.key = std::move(tokens[1]);
        }
        return Result<Command>::success(std::move(command));
    }

    if (name == "STATS") {
        const Status sizeStatus = requireSize(tokens, 1, "STATS");
        if (!sizeStatus.ok()) {
            return Result<Command>::failure(sizeStatus);
        }
        command.type = CommandType::Stats;
        return Result<Command>::success(std::move(command));
    }

    if (name == "COMPACT") {
        const Status sizeStatus = requireSize(tokens, 1, "COMPACT");
        if (!sizeStatus.ok()) {
            return Result<Command>::failure(sizeStatus);
        }
        command.type = CommandType::Compact;
        return Result<Command>::success(std::move(command));
    }

    if (name == "HELP") {
        const Status sizeStatus = requireSize(tokens, 1, "HELP");
        if (!sizeStatus.ok()) {
            return Result<Command>::failure(sizeStatus);
        }
        command.type = CommandType::Help;
        return Result<Command>::success(std::move(command));
    }

    if (name == "EXIT" || name == "QUIT") {
        const Status sizeStatus = requireSize(tokens, 1, "EXIT");
        if (!sizeStatus.ok()) {
            return Result<Command>::failure(sizeStatus);
        }
        command.type = CommandType::Exit;
        return Result<Command>::success(std::move(command));
    }

    return Result<Command>::failure(Status::invalidArgument("unknown command: " + tokens.front()));
}

} // namespace minikv

