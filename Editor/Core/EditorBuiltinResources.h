#pragma once

#include "Engine/Runtime/Core/Types.h"

#include <vector>

namespace ve::editor
{
    enum class BuiltinGizmoIcon
    {
        Camera,
        Light,
    };

    struct BuiltinGizmoIconUvRect
    {
        Float32 minU = 0.0f;
        Float32 minV = 0.0f;
        Float32 maxU = 1.0f;
        Float32 maxV = 1.0f;
    };

    struct BuiltinGizmoIconAtlas
    {
        UInt32 width = 0;
        UInt32 height = 0;
        UInt32 rowPitch = 0;
        std::vector<UInt8> pixels;
    };

    [[nodiscard]] BuiltinGizmoIconAtlas GenerateBuiltinGizmoIconAtlas();
    [[nodiscard]] BuiltinGizmoIconUvRect GetBuiltinGizmoIconUvRect(BuiltinGizmoIcon icon) noexcept;
} // namespace ve::editor
