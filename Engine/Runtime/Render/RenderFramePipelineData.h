#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>

namespace ve
{
    class FrameContext;
    class FrameTransientResourcePool;
    class RHIPipelineManager;
    struct FrameGraphDebugFrameCapture;
    class RTCamera;
    class RTMaterialResource;
    class RTRenderItem;
    class RTRenderViewState;
    class RTScene;
    struct BuiltInShaderResources;
    class VirtualShadowManager;
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
        UInt32 frameSlotIndex = 0;
        rhi::RhiDevice* device = nullptr;
        rhi::RhiSwapchain* mainSwapchain = nullptr;
        RHIPipelineManager* pipelineManager = nullptr;
        FrameContext* frameContext = nullptr;
        FrameTransientResourcePool* transientResourcePool = nullptr;
        VirtualShadowManager* virtualShadowManager = nullptr;
        FrameGraphDebugFrameCapture* frameGraphDebugCapture = nullptr;
        const BuiltInShaderResources* builtInShaderResources = nullptr;

        [[nodiscard]] rhi::RhiCommandList& GetCommandList() const;
        [[nodiscard]] UniformBufferAllocation UploadTransientUniform(const void* data, UInt64 size, const char* debugName) const;
        void AdoptTransientRhiObject(std::shared_ptr<rhi::RhiObject> object) const;
        [[nodiscard]] UniformBufferAllocation GetSceneUniform(RTScene& scene) const;
        [[nodiscard]] UniformBufferAllocation GetViewUniform(RTRenderViewState& viewState,
                                                             const RTCamera* camera,
                                                             rhi::RhiExtent2D targetExtent) const;
        [[nodiscard]] UniformBufferAllocation GetObjectUniform(RTRenderItem& item) const;
        [[nodiscard]] UniformBufferAllocation GetMaterialUniform(RTMaterialResource& material) const;
    };
} // namespace ve
