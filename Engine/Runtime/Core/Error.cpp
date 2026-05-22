#include "Engine/Runtime/Core/Error.h"

#include <utility>

namespace ve
{
Error::Error() noexcept = default;

Error::Error(ErrorCode code) noexcept
    : code_(code)
{
}

Error::Error(ErrorCode code, std::string message)
    : code_(code)
    , message_(std::move(message))
{
}

bool Error::IsOk() const noexcept
{
    return code_ == ErrorCode::None;
}

ErrorCode Error::GetCode() const noexcept
{
    return code_;
}

const std::string& Error::GetMessage() const noexcept
{
    return message_;
}

const char* ToString(ErrorCode code) noexcept
{
    switch (code)
    {
    case ErrorCode::None:
        return "None";
    case ErrorCode::Unknown:
        return "Unknown";
    case ErrorCode::InvalidArgument:
        return "InvalidArgument";
    case ErrorCode::InvalidState:
        return "InvalidState";
    case ErrorCode::NotFound:
        return "NotFound";
    case ErrorCode::AlreadyExists:
        return "AlreadyExists";
    case ErrorCode::OutOfMemory:
        return "OutOfMemory";
    case ErrorCode::IOError:
        return "IOError";
    case ErrorCode::PlatformError:
        return "PlatformError";
    case ErrorCode::Unsupported:
        return "Unsupported";
    case ErrorCode::Timeout:
        return "Timeout";
    case ErrorCode::Cancelled:
        return "Cancelled";
    }

    return "Unknown";
}
}
