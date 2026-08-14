#include "minikv/Status.h"

#include <utility>

namespace minikv {

Status::Status(StatusCode code, std::string message)
    : code_(code)
    , message_(std::move(message))
{
}

Status Status::success()
{
    return {};
}

Status Status::notFound(std::string message)
{
    return {StatusCode::NotFound, std::move(message)};
}

Status Status::invalidArgument(std::string message)
{
    return {StatusCode::InvalidArgument, std::move(message)};
}

Status Status::ioError(std::string message)
{
    return {StatusCode::IoError, std::move(message)};
}

Status Status::corruption(std::string message)
{
    return {StatusCode::Corruption, std::move(message)};
}

Status Status::closed(std::string message)
{
    return {StatusCode::Closed, std::move(message)};
}

bool Status::ok() const noexcept
{
    return code_ == StatusCode::Ok;
}

StatusCode Status::code() const noexcept
{
    return code_;
}

const std::string& Status::message() const noexcept
{
    return message_;
}

std::string Status::toString() const
{
    if (ok()) {
        return "OK";
    }
    return std::string(statusCodeName(code_)) + ": " + message_;
}

std::string_view statusCodeName(StatusCode code) noexcept
{
    switch (code) {
    case StatusCode::Ok:
        return "OK";
    case StatusCode::NotFound:
        return "NOT_FOUND";
    case StatusCode::InvalidArgument:
        return "INVALID_ARGUMENT";
    case StatusCode::IoError:
        return "IO_ERROR";
    case StatusCode::Corruption:
        return "CORRUPTION";
    case StatusCode::Closed:
        return "CLOSED";
    }
    return "UNKNOWN";
}

} // namespace minikv

