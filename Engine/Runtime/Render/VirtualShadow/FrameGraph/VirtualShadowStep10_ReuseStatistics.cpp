#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"

#include <iterator>

namespace ve
{
    namespace
    {
        inline constexpr const char* Step10_ReuseStatisticsComputeHlsl = R"(
RWStructuredBuffer<uint> Statistics : register(u0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint retainedRequested = Statistics[1];
    Statistics[2] = retainedRequested;
    Statistics[3] = 0u;
    Statistics[4] = 0u;
    Statistics[5] = 0u;
}
)";

        struct Step10_ReuseStatisticsPassData
        {
            FrameGraphBufferHandle statistics;
        };

        void RecordStep10_ReuseStatistics(const FrameRenderPipelineData& frameData, rhi::RhiBuffer& statistics)
        {
            if (statistics.GetSize() < sizeof(VirtualShadowGpuStatistics))
            {
                FailVirtualShadow("VSM Step10_ReuseStatistics requires the complete statistics payload.");
            }

            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
            };
            rhi::RhiComputePipelineState* pipeline = virtual_shadow_detail::GetVirtualShadowComputePipeline(frameData,
                                                                                                            "VirtualShadowStep10_ReuseStatistics",
                                                                                                            "VirtualShadow.Step10_ReuseStatistics.Compute",
                                                                                                            Step10_ReuseStatisticsComputeHlsl,
                                                                                                            bindings,
                                                                                                            static_cast<UInt32>(std::size(bindings)));
            if (pipeline == nullptr)
            {
                FailVirtualShadow("VSM Step10_ReuseStatistics failed to create its compute pipeline.");
            }

            rhi::RhiCommandList& commandList = frameData.GetCommandList();
            commandList.SetComputePipeline(*pipeline);
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, statistics, 0, statistics.GetSize());
            commandList.Dispatch(1, 1, 1);
        }
    } // namespace

    void AddVirtualShadowStep10_ReuseStatisticsPass(FrameGraph& frameGraph, VirtualShadowFrameGraphResources& resources)
    {
        if (!resources.statistics.IsValid())
        {
            FailVirtualShadow("VSM Step10_ReuseStatistics registration requires the statistics resource.");
        }
        frameGraph.AddComputePass<Step10_ReuseStatisticsPassData>(
            "VirtualShadow.Step10_ReuseStatistics",
            [&resources](FrameGraphBuilder& builder, Step10_ReuseStatisticsPassData& data)
            {
                data.statistics = builder.Write(resources.statistics);
                resources.statistics = data.statistics;
            },
            [](const Step10_ReuseStatisticsPassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            { RecordStep10_ReuseStatistics(context.frameData, virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.statistics)); });
    }
} // namespace ve
