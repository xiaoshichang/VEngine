#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h"

#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Resource/BuiltInShaderLibrary.h"
#include "Engine/Runtime/Render/RHIPipelineManager.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"

namespace ve::virtual_shadow_detail
{
    rhi::RhiComputePipelineState* GetVirtualShadowComputePipeline(const FrameRenderPipelineData& frameData,
                                                                  const char* name,
                                                                  const rhi::RhiPipelineResourceBindingDesc* bindings,
                                                                  UInt32 bindingCount)
    {
        if (rhi::RhiComputePipelineState* cached = frameData.pipelineManager->GetComputePipeline(ComputePipelineID{name, 0}); cached != nullptr)
        {
            return cached;
        }
        if (frameData.builtInShaderResources == nullptr || frameData.builtInShaderResources->virtualShadow == nullptr)
        {
            FailVirtualShadow("VSM render shader resource is unavailable.");
        }
        const RTShaderPass* shaderPass = frameData.builtInShaderResources->virtualShadow->GetPass(name);
        if (shaderPass == nullptr || shaderPass->GetComputeShader() == nullptr)
        {
            FailVirtualShadow((std::string("VSM shader pass is unavailable: ") + name).c_str());
        }

        rhi::RhiComputePipelineDesc desc = {};
        desc.computeShader = shaderPass->GetComputeShader();
        desc.resourceLayout = {bindings, bindingCount};
        desc.debugName = name;
        return frameData.pipelineManager->GetOrCreateComputePipeline(*frameData.device, ComputePipelineID{name, 0}, desc);
    }

    UniformBufferAllocation UploadVirtualShadowPassConstants(const FrameRenderPipelineData& frameData, const VirtualShadowGpuConstants& constants)
    {
        const UniformBufferAllocation allocation =
            frameData.UploadTransientUniform(&constants, sizeof(constants), "VirtualShadowPassUniform");
        if (allocation.buffer == nullptr)
        {
            FailVirtualShadow("VSM pass failed to upload its frame constants.");
        }
        return allocation;
    }

    VirtualShadowRequestRecordingContext BuildVirtualShadowRequestRecordingContext(const FrameRenderPipelineData& frameData,
                                                                                   const VirtualShadowFramePacket& packet,
                                                                                   VirtualShadowPageTableSlice slice,
                                                                                   UInt32 physicalPageCapacity) noexcept
    {
        return {frameData, packet, slice, physicalPageCapacity};
    }

    VirtualShadowGpuConstants BuildVirtualShadowRequestGpuConstants(const VirtualShadowRequestRecordingContext& context, UInt32 passLevel) noexcept
    {
        VirtualShadowGpuConstants constants = BuildVirtualShadowGpuConstants(context.packet);
        constants.physicalPageCapacity = context.physicalPageCapacity;
        constants.passLevel = passLevel;
        constants.resetCache = 0;
        if (context.packet.invalidateViewPages)
        {
            constants.invalidationCount = InvalidVirtualShadowGpuInvalidationCount;
        }
        return constants;
    }

    VirtualShadowGpuConstants BuildVirtualShadowPageGpuConstants(const VirtualShadowPageRecordingContext& context) noexcept
    {
        VirtualShadowGpuConstants constants = BuildVirtualShadowGpuConstants(context.packet);
        constants.physicalPageCapacity = context.physicalPageCapacity;
        constants.resetCache = 0;
        if (context.packet.invalidateViewPages)
        {
            constants.invalidationCount = InvalidVirtualShadowGpuInvalidationCount;
        }
        return constants;
    }

    VirtualShadowRequestRecordingContext BuildVirtualShadowRequestPassContext(const VirtualShadowViewRequestPassData& data, const RenderPassContext& context)
    {
        static_cast<void>(context.GetView(data.viewIndex));
        return BuildVirtualShadowRequestRecordingContext(context.frameData, data.packet, data.slice, data.physicalPageCapacity);
    }

    VirtualShadowRequestRecordingContext BuildVirtualShadowResidencyPassContext(const VirtualShadowResidencyPassData& data, const RenderPassContext& context)
    {
        static_cast<void>(context.GetView(data.viewIndex));
        return BuildVirtualShadowRequestRecordingContext(context.frameData, data.packet, data.slice, data.physicalPageCapacity);
    }

    VirtualShadowPageRecordingContext BuildVirtualShadowPagePassContext(const VirtualShadowViewPagePassData& data, const RenderPassContext& context)
    {
        return {context.frameData, data.packet, data.slice, data.physicalPageCapacity, context.rendererData.opaqueItems, data.drawDiagnostics};
    }

    UInt32 GetVirtualShadowPhysicalPageDispatchGroupCount(UInt32 physicalPageCapacity) noexcept
    {
        return (physicalPageCapacity + 63u) / 64u;
    }

    rhi::RhiBuffer& RequireVirtualShadowBuffer(const FrameGraphPassResources& resources, FrameGraphBufferHandle handle)
    {
        rhi::RhiBuffer* buffer = resources.GetBuffer(handle).buffer;
        if (buffer == nullptr)
        {
            FailVirtualShadow("VSM pass requires every declared buffer backing.");
        }
        return *buffer;
    }

    rhi::RhiTexture& RequireVirtualShadowTexture(const FrameGraphPassResources& resources, FrameGraphTextureHandle handle)
    {
        rhi::RhiTexture* texture = resources.GetTexture(handle).texture;
        if (texture == nullptr)
        {
            FailVirtualShadow("VSM pass requires every declared texture backing.");
        }
        return *texture;
    }

    void ValidateVirtualShadowRequestView(const VirtualShadowPreparedViewGraphData& view, bool requireReceiverDepth)
    {
        if (!view.packet.valid || view.packet.viewID == InvalidVirtualShadowViewID || view.packet.viewID > VirtualShadowMaximumViewID ||
            !view.packet.clipmaps.valid || !view.slice.IsValid() || (requireReceiverDepth && !view.receiverDepth.IsValid()))
        {
            FailVirtualShadow("VSM request-pass registration requires one complete active prepared view.");
        }
    }

    void ValidateVirtualShadowResidencyView(const VirtualShadowPreparedViewGraphData& view)
    {
        if (!view.packet.valid || view.packet.viewID == InvalidVirtualShadowViewID || view.packet.viewID > VirtualShadowMaximumViewID ||
            !view.packet.clipmaps.valid || !view.slice.IsValid())
        {
            FailVirtualShadow("VSM residency-pass registration requires one complete active prepared view.");
        }
    }

    void ValidateVirtualShadowPageView(const VirtualShadowPreparedViewGraphData& view, const VirtualShadowFrameGraphResources& resources)
    {
        if (!view.packet.valid || view.packet.viewID == InvalidVirtualShadowViewID || view.packet.viewID > VirtualShadowMaximumViewID ||
            !view.packet.clipmaps.valid || !view.slice.IsValid() || view.slice.pageTableOffset % sizeof(UInt32) != 0 || !resources.atlas.IsValid() ||
            !resources.pageTable.IsValid() || !resources.physicalPages.IsValid() || resources.physicalPageCapacity == 0)
        {
            FailVirtualShadow("VSM page-pass registration requires a complete active prepared view, aligned page-table slice, and resource bundle.");
        }
    }

    void ValidateVirtualShadowPhysicalCapacity(UInt32 physicalPageCapacity)
    {
        if (physicalPageCapacity == 0 || physicalPageCapacity > VirtualShadowMaxPhysicalPageCount)
        {
            FailVirtualShadow("VSM pass registration requires the allocated physical-page capacity.");
        }
    }
} // namespace ve::virtual_shadow_detail
