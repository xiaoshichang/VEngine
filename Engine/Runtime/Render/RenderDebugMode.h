#pragma once

#include "Engine/Runtime/Core/Types.h"

namespace ve
{
    enum class RenderDebugMode : UInt8
    {
        None = 0,
        VsmRedraw,
        ShadowCasterDirty,
        Count,
    };
} // namespace ve
