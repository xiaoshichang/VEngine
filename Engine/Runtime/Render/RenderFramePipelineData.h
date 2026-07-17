#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>

namespace ve
{
    class ShaderManager;
    class FrameContext;
    class FrameGraphTransientResourcePool;
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
        rhi::RhiSwapchain* mainSwapchain = nullptr;
        ShaderManager* shaderManager = nullptr;
        FrameContext* frameContext = nullptr;

        [[nodiscard]] rhi::RhiCommandList& GetCommandList() const;
        [[nodiscard]] FrameGraphTransientResourcePool& GetFrameGraphTransientResourcePool() const;
        void RetainTransientResource(std::unique_ptr<rhi::RhiObject> resource) const;
        [[nodiscard]] UniformBufferAllocation UploadUniform(const void* data, UInt64 size) const;
        [[nodiscard]] UniformBufferAllocation GetFrameUniform(const RTScene& scene) const;
        [[nodiscard]] UniformBufferAllocation GetViewUniform(const RTCamera* camera, rhi::RhiExtent2D targetExtent) const;
        [[nodiscard]] UniformBufferAllocation GetObjectUniform(const RTRenderItem& item) const;
    };
} // namespace ve
