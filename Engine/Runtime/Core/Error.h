#pragma once

#include <string>

namespace ve
{
    /// Common recoverable error codes shared by first-stage engine modules.
    enum class ErrorCode
    {
        None,
        Unknown,
        InvalidArgument,
        InvalidState,
        NotFound,
        AlreadyExists,
        OutOfMemory,
        IOError,
        PlatformError,
        Unsupported,
        Timeout,
        Cancelled,
    };

    /// Owns a recoverable error code and optional human-readable diagnostic message.
    class Error
    {
    public:
        Error() noexcept;
        explicit Error(ErrorCode code) noexcept;
        Error(ErrorCode code, std::string message);

        [[nodiscard]] bool IsOk() const noexcept;
        [[nodiscard]] ErrorCode GetCode() const noexcept;
        [[nodiscard]] const std::string& GetMessage() const noexcept;

    private:
        ErrorCode code_ = ErrorCode::None;
        std::string message_;
    };

    /// Returns a stable, non-localized name for an error code.
    [[nodiscard]] const char* ToString(ErrorCode code) noexcept;
} // namespace ve
