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
        void RecordStep4_ResolvePageHits(const VirtualShadowRequestRecordingContext& context,
                                         bool countPersistedRequests,
                                         rhi::RhiBuffer& requestList,
                                         rhi::RhiBuffer& requestCounts,
                                         rhi::RhiBuffer& pageTable,
                                         rhi::RhiBuffer& physicalPages,
                                         rhi::RhiBuffer& statistics)
        {
            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
                {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Compute, 0},
                {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Compute, 1},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 2},
            };
            rhi::RhiComputePipelineState* pipeline = virtual_shadow_detail::GetVirtualShadowComputePipeline(context.frameData,
                                                                                                            "VirtualShadowStep4_ResolvePageHits",
                                                                                                            "VirtualShadow.Step4_ResolvePageHits.Compute",
                                                                                                            bindings,
                                                                                                            static_cast<UInt32>(std::size(bindings)));
            if (pipeline == nullptr)
            {
                FailVirtualShadow("VSM Step4_ResolvePageHits requires its compute pipeline.");
            }

            const UniformBufferAllocation constants = virtual_shadow_detail::UploadVirtualShadowPassConstants(
                context.frameData, virtual_shadow_detail::BuildVirtualShadowRequestGpuConstants(context, countPersistedRequests ? 1u : 0u));
            rhi::RhiCommandList& commandList = context.frameData.GetCommandList();
            commandList.SetComputePipeline(*pipeline);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
            commandList.SetStorageBuffer(rhi::RhiShaderStage::Compute, 0, requestList, context.slice.requestListOffset, VirtualShadowRequestListSliceSize);
            commandList.SetStorageBuffer(
                rhi::RhiShaderStage::Compute, 1, requestCounts, context.slice.requestCountsOffset, VirtualShadowRequestCountBufferSize);
            commandList.SetReadWriteStorageBuffer(
                rhi::RhiShaderStage::Compute, 0, pageTable, context.slice.pageTableOffset, VirtualShadowLogicalPageBufferSize);
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 1, physicalPages, 0, physicalPages.GetSize());
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 2, statistics, 0, statistics.GetSize());
            commandList.Dispatch((VirtualShadowPagesPerAxis * VirtualShadowPagesPerAxis + 63u) / 64u, 1, 1);
        }
    } // namespace

    void AddVirtualShadowStep4_ResolvePageHitsPass(FrameGraph& graph,
                                                   const VirtualShadowPreparedViewGraphData& view,
                                                   VirtualShadowFrameGraphResources& resources,
                                                   bool uavBarrierBeforeExecute,
                                                   bool countPersistedRequests)
    {
        virtual_shadow_detail::ValidateVirtualShadowResidencyView(view);
        virtual_shadow_detail::ValidateVirtualShadowPhysicalCapacity(resources.physicalPageCapacity);
        graph.AddComputePass<virtual_shadow_detail::VirtualShadowResidencyPassData>(
            "VirtualShadow.Step4_ResolvePageHits[View=" + std::to_string(view.packet.viewID) + "]",
            [&resources, view, uavBarrierBeforeExecute, countPersistedRequests](FrameGraphBuilder& builder,
                                                                                virtual_shadow_detail::VirtualShadowResidencyPassData& data)
            {
                data.viewIndex = view.viewIndex;
                data.physicalPageCapacity = resources.physicalPageCapacity;
                data.packet = view.packet;
                data.slice = view.slice;
                data.countPersistedRequests = countPersistedRequests;
                data.requestList = builder.Read(resources.requestList);
                data.requestCounts = builder.Read(resources.requestCounts);
                data.pageTable = builder.Write(resources.pageTable);
                resources.pageTable = data.pageTable;
                data.physicalPages = builder.Write(resources.physicalPages);
                resources.physicalPages = data.physicalPages;
                data.statistics = builder.Write(resources.statistics);
                resources.statistics = data.statistics;
                if (uavBarrierBeforeExecute)
                {
                    builder.AddUavBarrierBeforeExecute(data.physicalPages);
                }
            },
            [](const virtual_shadow_detail::VirtualShadowResidencyPassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            {
                const VirtualShadowRequestRecordingContext gpuContext = virtual_shadow_detail::BuildVirtualShadowResidencyPassContext(data, context);
                RecordStep4_ResolvePageHits(gpuContext,
                                            data.countPersistedRequests,
                                            virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.requestList),
                                            virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.requestCounts),
                                            virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.pageTable),
                                            virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.physicalPages),
                                            virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.statistics));
            });
    }
} // namespace ve
