#pragma once

#include "Engine/Runtime/Core/Types.h"

#include <memory>

namespace ve
{
    class ShaderManager;
    class FrameContext;
    class RTCamera;
    class RTRenderItem;
    class RTScene;
    struct UniformBufferAllocation;

    namespace rhi
    {
        class RhiCommandList;
        class RhiDevice;
        class RhiObject;
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
        FrameContext* frameContext = nullptr;

        void RetainTransientResource(std::unique_ptr<rhi::RhiObject> resource) const;
        [[nodiscard]] UniformBufferAllocation UploadUniform(const void* data, UInt64 size) const;
        [[nodiscard]] UniformBufferAllocation GetFrameUniform(const RTScene& scene) const;
        [[nodiscard]] UniformBufferAllocation GetViewUniform(const RTCamera* camera) const;
        [[nodiscard]] UniformBufferAllocation GetObjectUniform(const RTRenderItem& item) const;
    };
} // namespace ve
