#pragma once

#include "minikv/Status.h"

#include <optional>
#include <stdexcept>
#include <utility>

namespace minikv {

template<typename T>
class Result final
{
public:
    [[nodiscard]] static Result success(T value)
    {
        return Result(Status::success(), std::move(value));
    }

    [[nodiscard]] static Result failure(Status status)
    {
        return Result(std::move(status), std::nullopt);
    }

    [[nodiscard]] bool ok() const noexcept
    {
        return status_.ok();
    }

    [[nodiscard]] const Status& status() const noexcept
    {
        return status_;
    }

    [[nodiscard]] const T& value() const
    {
        ensureValue();
        return *value_;
    }

    [[nodiscard]] T& value()
    {
        ensureValue();
        return *value_;
    }

    [[nodiscard]] T take()
    {
        ensureValue();
        return std::move(*value_);
    }

private:
    Result(Status status, std::optional<T> value)
        : status_(std::move(status))
        , value_(std::move(value))
    {
    }

    void ensureValue() const
    {
        if (!ok() || !value_.has_value()) {
            throw std::logic_error("attempted to access a failed Result");
        }
    }

    Status status_;
    std::optional<T> value_;
};

} // namespace minikv

