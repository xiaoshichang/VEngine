#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h"

#include <iterator>
#include <string>

namespace ve
{
    namespace
    {
        struct Step9_FinalizeScenePassData
        {
            UInt32 atlasExtent = 0;
            UInt32 physicalPageCapacity = 0;
            FrameGraphBufferHandle physicalPages;
            FrameGraphBufferHandle statistics;
        };

        void RecordStep9_FinalizeScene(const FrameRenderPipelineData& frameData,
                                       UInt32 atlasExtent,
                                       UInt32 physicalPageCapacity,
                                       rhi::RhiBuffer& physicalPages,
                                       rhi::RhiBuffer& statistics)
        {
            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
            };
            rhi::RhiComputePipelineState* pipeline = virtual_shadow_detail::GetVirtualShadowComputePipeline(frameData,
                                                                                                            "VirtualShadowStep9_FinalizeScene",
                                                                                                            bindings,
                                                                                                            static_cast<UInt32>(std::size(bindings)));
            if (pipeline == nullptr)
            {
                FailVirtualShadow("VSM Step9_FinalizeScene failed to create its compute pipeline.");
            }

            VirtualShadowGpuConstants values = {};
            values.atlasExtent = atlasExtent;
            values.physicalPageCapacity = physicalPageCapacity;
            values.frameIndex = static_cast<UInt32>(frameData.frameIndex);
            const UniformBufferAllocation constants = virtual_shadow_detail::UploadVirtualShadowPassConstants(frameData, values);
            rhi::RhiCommandList& commandList = frameData.GetCommandList();
            commandList.SetComputePipeline(*pipeline);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, physicalPages, 0, physicalPages.GetSize());
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 1, statistics, 0, statistics.GetSize());
            commandList.Dispatch((physicalPageCapacity + 63u) / 64u, 1, 1);
        }
    } // namespace

    void
    AddVirtualShadowStep9_FinalizeScenePass(FrameGraph& frameGraph, VirtualShadowPreparedFamilyGraphData& family, VirtualShadowFrameGraphResources& resources)
    {
        if (family.sceneCache == nullptr || family.views.empty() || resources.physicalPageCapacity == 0)
        {
            FailVirtualShadow("VSM Step9_FinalizeScene registration requires a complete prepared family.");
        }
        const UInt32 atlasExtent = family.sceneCache->GetDesc().atlasExtent;
        frameGraph.AddComputePass<Step9_FinalizeScenePassData>(
            "VirtualShadow.Step9_FinalizeScene",
            [&family, &resources, atlasExtent](FrameGraphBuilder& builder, Step9_FinalizeScenePassData& data)
            {
                data.atlasExtent = atlasExtent;
                data.physicalPageCapacity = resources.physicalPageCapacity;
                data.physicalPages = builder.Write(resources.physicalPages);
                resources.physicalPages = data.physicalPages;
                family.physicalPages = data.physicalPages;
                data.statistics = builder.Write(resources.statistics);
                resources.statistics = data.statistics;
                family.statistics = data.statistics;
                builder.AddUavBarrierBeforeExecute(data.physicalPages);
                builder.AddUavBarrierBeforeExecute(data.statistics);
                family.atlas = resources.atlas;
            },
            [](const Step9_FinalizeScenePassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            {
                RecordStep9_FinalizeScene(context.frameData,
                                          data.atlasExtent,
                                          data.physicalPageCapacity,
                                          virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.physicalPages),
                                          virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.statistics));
            });
    }
} // namespace ve
