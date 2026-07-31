#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderPerformanceStatistics.h"

namespace ve::editor
{
    inline constexpr Float32 EditorPerformanceFooterHeight = 74.0F;
    inline constexpr Float32 CommonFooterColumnWeight = 0.18F;
    inline constexpr Float32 RenderFooterColumnWeight = 0.18F;
    inline constexpr Float32 VirtualShadowFooterColumnWeight = 0.46F;
    inline constexpr Float32 PhysicsFooterColumnWeight = 0.18F;

    struct EditorPerformanceFooterSection
    {
        bool available = false;
    };

    struct EditorPerformanceFooterModel
    {
        Float32 framesPerSecond = 0.0F;
        EditorPerformanceFooterSection render;
        VirtualShadowPerformanceStatistics virtualShadow;
        EditorPerformanceFooterSection physics;
    };

    [[nodiscard]] inline EditorPerformanceFooterModel
    BuildEditorPerformanceFooterModel(Float32 framesPerSecond, const RenderPerformanceStatistics& statistics) noexcept
    {
        EditorPerformanceFooterModel model;
        model.framesPerSecond = framesPerSecond;
        model.virtualShadow = statistics.virtualShadow;
        return model;
    }
} // namespace ve::editor
