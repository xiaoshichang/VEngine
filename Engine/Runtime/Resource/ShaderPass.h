#pragma once

#include "Engine/Runtime/Core/Result.h"

#include <string_view>

namespace ve
{
    /// Stable pass names used by FrameGraph scene passes.
    enum class ShaderPassType
    {
        DepthOnly,
        OpaqueForward,
        TransparentForward,
        ShadowCaster,
        Internal,
    };

    [[nodiscard]] const char* ToString(ShaderPassType type) noexcept;
    [[nodiscard]] Result<ShaderPassType> ParseShaderPassType(std::string_view value);
}
