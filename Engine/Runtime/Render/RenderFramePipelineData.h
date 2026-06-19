#pragma once

#include "Engine/Runtime/Core/Types.h"

namespace ve
{
    class ShaderManager;

    namespace rhi
    {
        class RhiCommandList;
        class RhiDevice;
        class RhiSwapchain;
    } // namespace rhi

    /// Data owned by one FrameRenderPipeline invocation.
    ///
    /// Its lifetime is one complete render frame. Frame pipelines use this to record all scene renderers, overlays,
    /// copies, and presentation-facing work into the frame command list.
    struct FrameRenderPipelineData
    {
        UInt64 frameIndex = 0;
        rhi::RhiDevice* device = nullptr;
        rhi::RhiCommandList* commandList = nullptr;
        rhi::RhiSwapchain* mainSwapchain = nullptr;
        ShaderManager* shaderManager = nullptr;
    };
} // namespace ve
