#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"

namespace ve
{
    namespace
    {
        struct Step11_StatisticsReadbackPassData
        {
            FrameGraphBufferHandle statistics;
            FrameGraphBufferHandle readback;
        };

        void RecordStep11_StatisticsReadback(const FrameRenderPipelineData& frameData, rhi::RhiBuffer& statistics, rhi::RhiBuffer& statisticsReadback)
        {
            if (statistics.GetSize() < sizeof(VirtualShadowGpuStatistics) || statisticsReadback.GetSize() < sizeof(VirtualShadowGpuStatistics) ||
                !frameData.GetCommandList().CopyBuffer(statistics, 0, statisticsReadback, 0, sizeof(VirtualShadowGpuStatistics)))
            {
                FailVirtualShadow("VSM Step11_StatisticsReadback failed to record its exact statistics copy.");
            }
        }
    } // namespace

    void AddVirtualShadowStep11_StatisticsReadbackPass(FrameGraph& frameGraph, VirtualShadowFrameGraphResources& resources)
    {
        if (!resources.statistics.IsValid() || !resources.statisticsReadback.IsValid())
        {
            FailVirtualShadow("VSM Step11_StatisticsReadback registration requires statistics and readback resources.");
        }
        frameGraph.AddComputePass<Step11_StatisticsReadbackPassData>(
            "VirtualShadow.Step11_StatisticsReadback",
            [&resources](FrameGraphBuilder& builder, Step11_StatisticsReadbackPassData& data)
            {
                data.statistics = builder.ReadCopySource(resources.statistics);
                data.readback = builder.WriteCopyDestination(resources.statisticsReadback);
                resources.statisticsReadback = data.readback;
                builder.SetSideEffect();
            },
            [](const Step11_StatisticsReadbackPassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            {
                RecordStep11_StatisticsReadback(context.frameData,
                                                virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.statistics),
                                                virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.readback));
            });
        frameGraph.Export(resources.statisticsReadback);
    }
} // namespace ve
