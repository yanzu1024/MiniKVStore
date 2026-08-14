#pragma once

#include <string>
#include <string_view>

namespace minikv {

enum class StatusCode
{
    Ok,
    NotFound,
    InvalidArgument,
    IoError,
    Corruption,
    Closed
};

class Status final
{
public:
    Status() = default;

    [[nodiscard]] static Status success();
    [[nodiscard]] static Status notFound(std::string message);
    [[nodiscard]] static Status invalidArgument(std::string message);
    [[nodiscard]] static Status ioError(std::string message);
    [[nodiscard]] static Status corruption(std::string message);
    [[nodiscard]] static Status closed(std::string message);

    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] StatusCode code() const noexcept;
    [[nodiscard]] const std::string& message() const noexcept;
    [[nodiscard]] std::string toString() const;

private:
    Status(StatusCode code, std::string message);

    StatusCode code_ = StatusCode::Ok;
    std::string message_;
};

[[nodiscard]] std::string_view statusCodeName(StatusCode code) noexcept;

} // namespace minikv

