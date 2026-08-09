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
        void RecordStep3_MarkRequests(const VirtualShadowRequestRecordingContext& context,
                                      rhi::RhiTexture& receiverDepth,
                                      rhi::RhiBuffer& pageMarks,
                                      rhi::RhiBuffer& requestList,
                                      rhi::RhiBuffer& requestCounts,
                                      rhi::RhiBuffer& statistics)
        {
            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
                {rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Compute, 0},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 2},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 3},
            };
            rhi::RhiComputePipelineState* pipeline = virtual_shadow_detail::GetVirtualShadowComputePipeline(context.frameData,
                                                                                                            "VirtualShadowStep3_MarkRequests",
                                                                                                            bindings,
                                                                                                            static_cast<UInt32>(std::size(bindings)));
            if (pipeline == nullptr)
            {
                FailVirtualShadow("VSM Step3_MarkRequests requires its compute pipeline.");
            }

            const UniformBufferAllocation constants = virtual_shadow_detail::UploadVirtualShadowPassConstants(
                context.frameData, virtual_shadow_detail::BuildVirtualShadowRequestGpuConstants(context));
            rhi::RhiCommandList& commandList = context.frameData.GetCommandList();
            commandList.SetComputePipeline(*pipeline);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
            commandList.SetTexture(rhi::RhiShaderStage::Compute, 0, receiverDepth);
            commandList.SetReadWriteStorageBuffer(
                rhi::RhiShaderStage::Compute, 0, pageMarks, context.slice.pageMarksOffset, VirtualShadowLogicalPageBufferSize);
            commandList.SetReadWriteStorageBuffer(
                rhi::RhiShaderStage::Compute, 1, requestList, context.slice.requestListOffset, VirtualShadowRequestListSliceSize);
            commandList.SetReadWriteStorageBuffer(
                rhi::RhiShaderStage::Compute, 2, requestCounts, context.slice.requestCountsOffset, VirtualShadowRequestCountBufferSize);
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 3, statistics, 0, statistics.GetSize());
            commandList.Dispatch((context.packet.screenWidth + 7u) / 8u, (context.packet.screenHeight + 7u) / 8u, 1);
        }
    } // namespace

    void AddVirtualShadowStep3_MarkRequestsPass(FrameGraph& graph,
                                                const VirtualShadowPreparedViewGraphData& view,
                                                VirtualShadowFrameGraphResources& resources,
                                                bool pageMarksBarrierBeforeExecute)
    {
        virtual_shadow_detail::ValidateVirtualShadowRequestView(view, true);
        virtual_shadow_detail::ValidateVirtualShadowPhysicalCapacity(resources.physicalPageCapacity);
        graph.AddComputePass<virtual_shadow_detail::VirtualShadowViewRequestPassData>(
            "VirtualShadow.Step3_MarkRequests[View=" + std::to_string(view.packet.viewID) + "]",
            [&resources, view, pageMarksBarrierBeforeExecute](FrameGraphBuilder& builder, virtual_shadow_detail::VirtualShadowViewRequestPassData& data)
            {
                data.viewIndex = view.viewIndex;
                data.packet = view.packet;
                data.slice = view.slice;
                data.physicalPageCapacity = resources.physicalPageCapacity;
                data.receiverDepth = builder.Read(view.receiverDepth);
                data.pageMarks = builder.Write(resources.pageMarks);
                resources.pageMarks = data.pageMarks;
                data.requestList = builder.Write(resources.requestList);
                resources.requestList = data.requestList;
                data.requestCounts = builder.Write(resources.requestCounts);
                resources.requestCounts = data.requestCounts;
                data.statistics = builder.Write(resources.statistics);
                resources.statistics = data.statistics;
                if (pageMarksBarrierBeforeExecute)
                {
                    builder.AddUavBarrierBeforeExecute(data.pageMarks);
                    builder.AddUavBarrierBeforeExecute(data.requestCounts);
                    builder.AddUavBarrierBeforeExecute(data.statistics);
                }
            },
            [](const virtual_shadow_detail::VirtualShadowViewRequestPassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            {
                const VirtualShadowRequestRecordingContext gpuContext = virtual_shadow_detail::BuildVirtualShadowRequestPassContext(data, context);
                RecordStep3_MarkRequests(gpuContext,
                                         virtual_shadow_detail::RequireVirtualShadowTexture(passResources, data.receiverDepth),
                                         virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.pageMarks),
                                         virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.requestList),
                                         virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.requestCounts),
                                         virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.statistics));
            });
    }
} // namespace ve
