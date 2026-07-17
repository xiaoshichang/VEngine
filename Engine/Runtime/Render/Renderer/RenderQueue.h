#pragma once

#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"

#include <string_view>

namespace ve
{
    /// Controls when a material's render items are submitted relative to other scene geometry.
    enum class RenderQueue : UInt16
    {
        Opaque = 2000,
        Transparent = 3000,
    };

    /// Returns the stable material descriptor spelling for a render queue.
    [[nodiscard]] const char* ToString(RenderQueue renderQueue) noexcept;

    /// Parses an exact, case-sensitive material descriptor spelling.
    [[nodiscard]] Result<RenderQueue> ParseRenderQueue(std::string_view value);
} // namespace ve
