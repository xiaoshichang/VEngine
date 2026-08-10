#pragma once

#include "Engine/Runtime/Application/Application.h"
#include "Engine/Runtime/Core/Platform.h"

#include <span>
#include <string>

namespace ve
{
    class ApplicationInitParamOptionParser final
    {
    public:
        [[nodiscard]] static Result<ApplicationInitParam> Parse(std::span<const std::string> arguments);
        [[nodiscard]] static Result<ApplicationInitParam> Parse(int argumentCount, char* arguments[]);

#if VE_PLATFORM_WINDOWS
        [[nodiscard]] static Result<ApplicationInitParam> ParseCurrentProcessCommandLine();
#endif
    };
} // namespace ve
