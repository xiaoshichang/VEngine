#pragma once

namespace ve
{
struct BuildInfo
{
    const char* projectName = "VEngine";
    const char* version = "0.0.0";
    const char* platform = "Unknown";
};

[[nodiscard]] BuildInfo GetBuildInfo() noexcept;
}
