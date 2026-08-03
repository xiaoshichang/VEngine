#pragma once

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

#include <exception>

namespace ve
{
    [[noreturn]] inline void FailVirtualShadow(const char* message)
    {
        VE_LOG_ERROR_CATEGORY("VirtualShadow", "{}", message);
        VE_ASSERT_ALWAYS_MESSAGE(false, message);
        std::terminate();
    }
} // namespace ve
