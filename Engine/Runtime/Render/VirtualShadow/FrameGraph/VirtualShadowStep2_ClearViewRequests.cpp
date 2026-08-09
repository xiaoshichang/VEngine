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
        void RecordStep2_ClearViewRequests(const VirtualShadowRequestRecordingContext& context,
                                           rhi::RhiBuffer& pageMarks,
                                           rhi::RhiBuffer& requestCounts,
                                           rhi::RhiBuffer& physicalPages)
        {
            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 2},
            };
            rhi::RhiComputePipelineState* pipeline = virtual_shadow_detail::GetVirtualShadowComputePipeline(context.frameData,
                                                                                                            "VirtualShadowStep2_ClearViewRequests",
                                                                                                            bindings,
                                                                                                            static_cast<UInt32>(std::size(bindings)));
            if (pipeline == nullptr)
            {
                FailVirtualShadow("VSM Step2_ClearViewRequests requires its compute pipeline.");
            }

            const UniformBufferAllocation constants = virtual_shadow_detail::UploadVirtualShadowPassConstants(
                context.frameData, virtual_shadow_detail::BuildVirtualShadowRequestGpuConstants(context));
            rhi::RhiCommandList& commandList = context.frameData.GetCommandList();
            commandList.SetComputePipeline(*pipeline);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
            commandList.SetReadWriteStorageBuffer(
                rhi::RhiShaderStage::Compute, 0, pageMarks, context.slice.pageMarksOffset, VirtualShadowLogicalPageBufferSize);
            commandList.SetReadWriteStorageBuffer(
                rhi::RhiShaderStage::Compute, 1, requestCounts, context.slice.requestCountsOffset, VirtualShadowRequestCountBufferSize);
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 2, physicalPages, 0, physicalPages.GetSize());
            const UInt32 itemCount = context.packet.resetSceneCache ? VirtualShadowLogicalPageCount : context.physicalPageCapacity;
            commandList.Dispatch((itemCount + 63u) / 64u, 1, 1);
        }
    } // namespace

    void AddVirtualShadowStep2_ClearViewRequestsPass(FrameGraph& graph,
                                                     const VirtualShadowPreparedViewGraphData& view,
                                                     VirtualShadowFrameGraphResources& resources,
                                                     bool uavBarrierBeforeExecute)
    {
        virtual_shadow_detail::ValidateVirtualShadowRequestView(view, false);
        virtual_shadow_detail::ValidateVirtualShadowPhysicalCapacity(resources.physicalPageCapacity);
        graph.AddComputePass<virtual_shadow_detail::VirtualShadowViewRequestPassData>(
            "VirtualShadow.Step2_ClearViewRequests[View=" + std::to_string(view.packet.viewID) + "]",
            [&resources, view, uavBarrierBeforeExecute](FrameGraphBuilder& builder, virtual_shadow_detail::VirtualShadowViewRequestPassData& data)
            {
                data.viewIndex = view.viewIndex;
                data.packet = view.packet;
                data.slice = view.slice;
                data.physicalPageCapacity = resources.physicalPageCapacity;
                data.pageMarks = builder.Write(resources.pageMarks);
                resources.pageMarks = data.pageMarks;
                data.requestCounts = builder.Write(resources.requestCounts);
                resources.requestCounts = data.requestCounts;
                data.physicalPages = builder.Write(resources.physicalPages);
                resources.physicalPages = data.physicalPages;
                if (uavBarrierBeforeExecute)
                {
                    builder.AddUavBarrierBeforeExecute(data.physicalPages);
                }
            },
            [](const virtual_shadow_detail::VirtualShadowViewRequestPassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            {
                const VirtualShadowRequestRecordingContext gpuContext = virtual_shadow_detail::BuildVirtualShadowRequestPassContext(data, context);
                RecordStep2_ClearViewRequests(gpuContext,
                                              virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.pageMarks),
                                              virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.requestCounts),
                                              virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.physicalPages));
            });
    }
} // namespace ve
