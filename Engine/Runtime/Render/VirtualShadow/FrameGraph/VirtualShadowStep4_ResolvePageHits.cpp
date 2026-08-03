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
        inline const std::string Step4_ResolvePageHitsComputeHlsl = std::string(virtual_shadow_detail::VirtualShadowCommonHlsl) + R"(
StructuredBuffer<uint> RequestList : register(t0);
StructuredBuffer<uint> RequestCounts : register(t1);
RWStructuredBuffer<uint> PageTable : register(u0);
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u1);
RWStructuredBuffer<uint> Statistics : register(u2);
[numthreads(64, 1, 1)]
void CSMain(uint requestIndex : SV_DispatchThreadID)
{
    if (requestIndex >= 16384u) return;
    for (uint level = 0u; level < clipmapCount; ++level)
    {
        if (requestIndex >= min(RequestCounts[level], 16384u)) continue;
        if (passLevel != 0u) InterlockedAdd(Statistics[1], 1u);

        uint logical = RequestList[level * 16384u + requestIndex];
        uint levelIndex = logical - level * 16384u;
        int2 localPage = int2(levelIndex & 127u, levelIndex >> 7u);
        int2 absolutePage = clipmaps[level].pageData.xy - int2(64, 64) + localPage;
        uint key0 = (uint(absolutePage.x) & 0xFFFFu) | ((uint(absolutePage.y) & 0xFFFFu) << 16u);
        uint key1 = level | ((viewID & 0x00FFFFFFu) << 8u);

        uint mappedPhysicalPlusOne = PageTable[logical];
        if (mappedPhysicalPlusOne != 0u && mappedPhysicalPlusOne <= physicalCapacity)
        {
            uint mappedPhysical = mappedPhysicalPlusOne - 1u;
            PhysicalPage mappedPage = PhysicalPages[mappedPhysical];
            if ((mappedPage.flags & 1u) != 0u && mappedPage.key0 == key0 && mappedPage.key1 == key1)
            {
                InterlockedMax(PhysicalPages[mappedPhysical].lastUsedFrame, frameIndex);
                InterlockedOr(PhysicalPages[mappedPhysical].flags, 4u);
                InterlockedAdd(Statistics[2], 1u);
                continue;
            }
        }
        PageTable[logical] = 0u;

        for (uint physical = 0u; physical < physicalCapacity; ++physical)
        {
            PhysicalPage page = PhysicalPages[physical];
            if ((page.flags & 1u) != 0u && page.key0 == key0 && page.key1 == key1)
            {
                InterlockedMax(PhysicalPages[physical].lastUsedFrame, frameIndex);
                InterlockedOr(PhysicalPages[physical].flags, 4u);
                PageTable[logical] = physical + 1u;
                InterlockedAdd(Statistics[2], 1u);
                break;
            }
        }
    }
}
)";

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
                                                                                                            Step4_ResolvePageHitsComputeHlsl.c_str(),
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
