#include "Engine/Runtime/Render/RenderFramePipelineData.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/FrameContext.h"
#include "Engine/Runtime/Render/FrameTransientResourcePool.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderViewState.h"

#include <utility>

namespace ve
{
    void FrameRenderPipelineData::RetainInFlightGpuFrameObject(std::shared_ptr<rhi::RhiObject> object) const
    {
        VE_ASSERT(frameContext != nullptr);
        frameContext->RetainInFlightGpuFrameObject(std::move(object));
    }

    rhi::RhiCommandList& FrameRenderPipelineData::GetCommandList() const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetCommandList();
    }

    UniformBufferAllocation FrameRenderPipelineData::UploadTransientUniform(const void* data, UInt64 size, const char* debugName) const
    {
        VE_ASSERT(transientResourcePool != nullptr);
        return transientResourcePool->UploadUniform(data, size, debugName);
    }

    void FrameRenderPipelineData::AdoptTransientRhiObject(std::shared_ptr<rhi::RhiObject> object) const
    {
        VE_ASSERT(transientResourcePool != nullptr);
        transientResourcePool->Adopt(std::move(object));
    }

    UniformBufferAllocation FrameRenderPipelineData::GetSceneUniform(RTScene& scene) const
    {
        VE_ASSERT(device != nullptr);
        return scene.GetSceneUniform(*device, frameSlotIndex, frameIndex);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetViewUniform(RTRenderViewState& viewState,
                                                                    const RTCamera* camera,
                                                                    rhi::RhiExtent2D targetExtent) const
    {
        VE_ASSERT(device != nullptr);
        return viewState.GetViewUniform(*device, frameSlotIndex, frameIndex, camera, targetExtent);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetObjectUniform(RTRenderItem& item) const
    {
        VE_ASSERT(device != nullptr);
        return item.GetObjectUniform(*device, frameSlotIndex);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetMaterialUniform(RTMaterialResource& material) const
    {
        VE_ASSERT(device != nullptr);
        return material.GetUniformBuffer(*device, frameSlotIndex);
    }
} // namespace ve
