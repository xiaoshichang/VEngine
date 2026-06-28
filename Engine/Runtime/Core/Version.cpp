#include "Engine/Runtime/Core/Version.h"

#ifndef VE_VERSION_STRING
#define VE_VERSION_STRING "0.0.0"
#endif

namespace ve
{
    BuildInfo GetBuildInfo() noexcept
    {
        const char* platform = "Unknown";

#if defined(VE_PLATFORM_WINDOWS) && VE_PLATFORM_WINDOWS
        platform = "Windows";
#elif defined(VE_PLATFORM_MACOS) && VE_PLATFORM_MACOS
        platform = "macOS";
#elif defined(VE_PLATFORM_APPLE) && VE_PLATFORM_APPLE
        platform = "Apple";
#endif

        BuildInfo buildInfo;
        buildInfo.projectName = "VEngine";
        buildInfo.version = VE_VERSION_STRING;
        buildInfo.platform = platform;
        return buildInfo;
    }
} // namespace ve
