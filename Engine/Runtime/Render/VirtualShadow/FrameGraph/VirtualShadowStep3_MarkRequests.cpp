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
        inline const std::string Step3_MarkRequestsComputeHlsl = std::string(virtual_shadow_detail::VirtualShadowCommonHlsl) + R"(
Texture2D<float> SceneDepth : register(t0);
RWStructuredBuffer<uint> PageMarks : register(u0);
RWStructuredBuffer<uint> RequestList : register(u1);
RWStructuredBuffer<uint> RequestCounts : register(u2);
RWStructuredBuffer<uint> Statistics : register(u3);

uint SelectClipmapLevel(float cameraDepth)
{
    uint preferredLevel = clipmapCount - 1u;
    [loop]
    for (uint level = 0u; level < clipmapCount; ++level)
    {
        if (cameraDepth <= clipmaps[level].radiusAndDepth.x)
        {
            preferredLevel = level;
            break;
        }
    }
    return preferredLevel;
}

bool IsPositionCoveredByClipmap(uint level, float2 lightPosition, out int2 localPage)
{
    float pageSize = clipmaps[level].originAndPageSize.w;
    int2 page = int2(floor(lightPosition / pageSize));
    int2 minimumPage = clipmaps[level].pageData.xy - int2(64, 64);
    localPage = page - minimumPage;
    return all(localPage >= 0) && all(localPage < 128);
}

void RequestPage(uint level, int2 localPage)
{
    uint logical = level * 16384u + uint(localPage.y) * 128u + uint(localPage.x);
    uint previousGeneration = 0u;
    uint generation = frameIndex + 1u;
    InterlockedExchange(PageMarks[logical], generation, previousGeneration);
    if (previousGeneration == generation)
    {
        return;
    }

    uint requestIndex = 0u;
    InterlockedAdd(RequestCounts[level], 1u, requestIndex);
    if (requestIndex < 16384u)
    {
        RequestList[level * 16384u + requestIndex] = logical;
        InterlockedAdd(Statistics[1], 1u);
    }
}

[numthreads(8, 8, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= screenWidth || id.y >= screenHeight) return;
    float depth = SceneDepth.Load(int3(id.xy, 0));
    if (depth >= 1.0f) return;
    float2 uv = (float2(id.xy) + 0.5f) / float2(screenWidth, screenHeight);
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(inverseViewProjection, clip);
    if (abs(world.w) < 1.0e-6f) return;
    world.xyz /= world.w;
    float2 lightPosition = float2(dot(world.xyz, lightRight.xyz), dot(world.xyz, lightUp.xyz));
    float cameraDepth = max(dot(world.xyz - cameraWorldPosition.xyz, cameraWorldForward.xyz), 0.0f);
    uint preferredLevel = SelectClipmapLevel(cameraDepth);
    [loop]
    for (uint level = preferredLevel; level < clipmapCount; ++level)
    {
        int2 localPage = int2(0, 0);
        if (!IsPositionCoveredByClipmap(level, lightPosition, localPage))
        {
            continue;
        }

        RequestPage(level, localPage);
        break;
    }
}
)";

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
                                                                                                            "VirtualShadow.Step3_MarkRequests.Compute",
                                                                                                            Step3_MarkRequestsComputeHlsl.c_str(),
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
