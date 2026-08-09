#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"

#include <iterator>
#include <string>

namespace ve
{
    namespace
    {
        void RecordStep8_MarkRendered(const VirtualShadowPageRecordingContext& context, rhi::RhiBuffer& physicalPages, rhi::RhiBuffer& statistics)
        {
            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
            };
            rhi::RhiComputePipelineState* pipeline = virtual_shadow_detail::GetVirtualShadowComputePipeline(context.frameData,
                                                                                                            "VirtualShadowStep8_MarkRendered",
                                                                                                            bindings,
                                                                                                            static_cast<UInt32>(std::size(bindings)));
            if (pipeline == nullptr)
            {
                FailVirtualShadow("VSM Step8_MarkRendered failed to create its compute pipeline.");
            }

            const UniformBufferAllocation constants =
                virtual_shadow_detail::UploadVirtualShadowPassConstants(context.frameData, virtual_shadow_detail::BuildVirtualShadowPageGpuConstants(context));
            rhi::RhiCommandList& commandList = context.frameData.GetCommandList();
            commandList.SetComputePipeline(*pipeline);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, physicalPages, 0, physicalPages.GetSize());
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 1, statistics, 0, statistics.GetSize());
            commandList.Dispatch((context.physicalPageCapacity + 63u) / 64u, 1, 1);
        }
    } // namespace

    void
    AddVirtualShadowStep8_MarkRenderedPass(FrameGraph& frameGraph, const VirtualShadowPreparedViewGraphData& view, VirtualShadowFrameGraphResources& resources)
    {
        virtual_shadow_detail::ValidateVirtualShadowPageView(view, resources);
        frameGraph.AddComputePass<virtual_shadow_detail::VirtualShadowViewPagePassData>(
            "VirtualShadow.Step8_MarkRendered[View=" + std::to_string(view.packet.viewID) + "]",
            [&resources, view](FrameGraphBuilder& builder, virtual_shadow_detail::VirtualShadowViewPagePassData& data)
            {
                data.viewIndex = view.viewIndex;
                data.packet = view.packet;
                data.physicalPageCapacity = resources.physicalPageCapacity;
                data.atlas = builder.Read(resources.atlas);
                data.physicalPages = builder.Write(resources.physicalPages);
                resources.physicalPages = data.physicalPages;
                data.statistics = builder.Write(resources.statistics);
                resources.statistics = data.statistics;
            },
            [](const virtual_shadow_detail::VirtualShadowViewPagePassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            {
                static_cast<void>(context.GetView(data.viewIndex));
                static_cast<void>(virtual_shadow_detail::RequireVirtualShadowTexture(passResources, data.atlas));
                RecordStep8_MarkRendered(virtual_shadow_detail::BuildVirtualShadowPagePassContext(data, context),
                                         virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.physicalPages),
                                         virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.statistics));
            });
    }
} // namespace ve
