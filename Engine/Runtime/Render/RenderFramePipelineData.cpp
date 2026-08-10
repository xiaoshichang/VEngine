#include "Engine/Runtime/Render/RenderFramePipelineData.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/FrameContext.h"
#include "Engine/Runtime/Render/RenderResource.h"

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

    UniformBufferAllocation FrameRenderPipelineData::UploadUniform(const void* data, UInt64 size) const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->UploadUniform(data, size);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetFrameUniform(const RTScene& scene) const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetFrameUniform(scene);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetViewUniform(const RTCamera* camera, rhi::RhiExtent2D targetExtent) const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetViewUniform(camera, targetExtent);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetObjectUniform(const RTRenderItem& item) const
    {
        VE_ASSERT(frameContext != nullptr);
        return frameContext->GetObjectUniform(item);
    }

    UniformBufferAllocation FrameRenderPipelineData::GetMaterialUniform(RTMaterialResource& material) const
    {
        VE_ASSERT(device != nullptr);
        return material.GetUniformBuffer(*device, frameSlotIndex);
    }
} // namespace ve
