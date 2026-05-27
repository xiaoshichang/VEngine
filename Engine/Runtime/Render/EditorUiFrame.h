#pragma once

#include "Engine/Runtime/Core/Types.h"

#include <vector>

namespace ve
{
    /// CPU-side copy of one ImGui vertex. The color is stored as RGBA8 bytes in little-endian memory order.
    struct EditorUiVertex
    {
        Float32 position[2] = {};
        Float32 uv[2] = {};
        UInt32 color = 0;
    };

    /// CPU-side copy of one ImGui draw command.
    struct EditorUiDrawCommand
    {
        UInt32 elementCount = 0;
        UInt32 indexOffset = 0;
        UInt32 vertexOffset = 0;
        Float32 clipRect[4] = {};
        UInt64 textureId = 0;
    };

    /// CPU-side copy of one ImGui draw list.
    struct EditorUiDrawList
    {
        std::vector<EditorUiVertex> vertices;
        std::vector<UInt32> indices;
        std::vector<EditorUiDrawCommand> commands;
    };

    /// Optional font atlas upload carried by the first Editor UI frame after an ImGui context is created.
    struct EditorUiFontAtlas
    {
        UInt32 width = 0;
        UInt32 height = 0;
        std::vector<UInt8> rgbaPixels;

        [[nodiscard]] bool IsValid() const noexcept
        {
            return width > 0 && height > 0 && !rgbaPixels.empty();
        }
    };

    /// Immutable UI frame data submitted from the Editor main thread and consumed on the Render Thread.
    struct EditorUiFrameData
    {
        Float32 displayPos[2] = {};
        Float32 displaySize[2] = {};
        Float32 framebufferScale[2] = {1.0f, 1.0f};
        EditorUiFontAtlas fontAtlas;
        std::vector<EditorUiDrawList> drawLists;

        [[nodiscard]] bool HasDrawableArea() const noexcept
        {
            return displaySize[0] > 0.0f && displaySize[1] > 0.0f;
        }
    };
} // namespace ve
