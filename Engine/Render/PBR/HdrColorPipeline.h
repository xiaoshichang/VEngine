#pragma once

#include "Engine/Render/PBR/PbrTypes.h"
#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Render/RenderTexture.h"

namespace ve::pbr
{
    /// Builds a sampled/renderable HDR scene-color description for the frame graph.
    [[nodiscard]] RenderTextureDesc BuildHdrSceneColorDesc(WindowExtent extent, const char* name = "HdrSceneColor");

    /// Returns whether a format can preserve HDR lighting values without clamping.
    [[nodiscard]] bool IsHdrFormat(rhi::RhiFormat format) noexcept;

    /// Clamps user-facing HDR settings to numerically safe ranges before upload.
    [[nodiscard]] HdrSettings SanitizeHdrSettings(HdrSettings settings) noexcept;
} // namespace ve::pbr
