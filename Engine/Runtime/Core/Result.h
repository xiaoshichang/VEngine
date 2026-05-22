#pragma once

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/Error.h"

#include <cstddef>
#include <type_traits>
#include <utility>
#include <variant>

namespace ve
{
/// Represents either a successful value of T or a recoverable Error.
template <typename T>
class Result
{
    static_assert(!std::is_void_v<T>, "Use Result<void> for void results.");
    static_assert(!std::is_reference_v<T>, "Result<T> does not support reference value types.");

public:
    /// Creates a successful result that owns the supplied value.
    [[nodiscard]] static Result Success(T value)
    {
        return Result(SuccessTag{}, std::move(value));
    }

    /// Creates a failed result. The supplied error is expected to be non-success.
    [[nodiscard]] static Result Failure(Error error)
    {
        VE_ASSERT_MESSAGE(!error.IsOk(), "Failure results must carry a non-success error code.");
        return Result(FailureTag{}, std::move(error));
    }

    [[nodiscard]] bool IsOk() const noexcept
    {
        return storage_.index() == ValueIndex;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return IsOk();
    }

    /// Returns the stored value. Calling this on a failed result is an API misuse.
    [[nodiscard]] T& GetValue()
    {
        VE_ASSERT_MESSAGE(IsOk(), "Result::GetValue called on a failed result.");
        return std::get<ValueIndex>(storage_);
    }

    /// Returns the stored value. Calling this on a failed result is an API misuse.
    [[nodiscard]] const T& GetValue() const
    {
        VE_ASSERT_MESSAGE(IsOk(), "Result::GetValue called on a failed result.");
        return std::get<ValueIndex>(storage_);
    }

    /// Moves the stored value out of the result. Calling this on a failed result is an API misuse.
    [[nodiscard]] T&& MoveValue()
    {
        VE_ASSERT_MESSAGE(IsOk(), "Result::MoveValue called on a failed result.");
        return std::move(std::get<ValueIndex>(storage_));
    }

    /// Returns the stored error. Calling this on a successful result is an API misuse.
    [[nodiscard]] const Error& GetError() const
    {
        VE_ASSERT_MESSAGE(!IsOk(), "Result::GetError called on a successful result.");
        return std::get<ErrorIndex>(storage_);
    }

private:
    struct SuccessTag
    {
    };

    struct FailureTag
    {
    };

    static constexpr std::size_t ValueIndex = 0;
    static constexpr std::size_t ErrorIndex = 1;

    Result(SuccessTag, T value)
        : storage_(std::in_place_index<ValueIndex>, std::move(value))
    {
    }

    Result(FailureTag, Error error)
        : storage_(std::in_place_index<ErrorIndex>, std::move(error))
    {
    }

    std::variant<T, Error> storage_;
};

/// Represents either success without a value or a recoverable Error.
template <>
class Result<void>
{
public:
    [[nodiscard]] static Result Success()
    {
        return Result();
    }

    /// Creates a failed result. The supplied error is expected to be non-success.
    [[nodiscard]] static Result Failure(Error error)
    {
        VE_ASSERT_MESSAGE(!error.IsOk(), "Failure results must carry a non-success error code.");
        return Result(std::move(error));
    }

    [[nodiscard]] bool IsOk() const noexcept
    {
        return ok_;
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return IsOk();
    }

    /// Returns the stored error. Calling this on a successful result is an API misuse.
    [[nodiscard]] const Error& GetError() const
    {
        VE_ASSERT_MESSAGE(!IsOk(), "Result<void>::GetError called on a successful result.");
        return error_;
    }

private:
    Result() noexcept = default;

    explicit Result(Error error)
        : ok_(false)
        , error_(std::move(error))
    {
    }

    bool ok_ = true;
    Error error_;
};
}
