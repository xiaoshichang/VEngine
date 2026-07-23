#include "Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderViewState.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <cstdint>
#include <iterator>
#include <string>

namespace ve
{
    namespace
    {
        inline constexpr const char* PassName = "GpuVirtualShadow";

        inline constexpr const char* CommonConstants = R"(
struct Clipmap { float4 originAndPageSize; float4 radiusAndDepth; int4 pageData; };
struct PageEntry { uint4 data; };
cbuffer ShadowConstants : register(b4)
{
    float4 lightRight; float4 lightUp; float4 lightForward; float4 atlasAndBias;
    Clipmap clipmaps[4];
    uint enabled; uint atlasExtent; uint physicalPageSize; uint clipmapCount;
    float4x4 inverseViewProjection;
    uint screenWidth; uint screenHeight; uint physicalCapacity; uint frameIndex;
    uint resetCache; uint gpuDriven; uint passLevel; uint invalidationCount;
    float4 cameraWorldPosition; float4 cameraWorldForward;
    PageEntry invalidatedPages[2048];
};
struct PhysicalPage { uint key0; uint key1; uint lastUsedFrame; uint flags; };
)";

        inline const std::string ClearComputeHlsl = std::string(CommonConstants) + R"(
RWStructuredBuffer<uint> PageMarks : register(u0);
RWStructuredBuffer<uint> PageTable : register(u1);
RWStructuredBuffer<uint> RequestCounts : register(u2);
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u3);
[numthreads(64, 1, 1)]
void CSMain(uint index : SV_DispatchThreadID)
{
    if (index < 65536u) { PageMarks[index] = 0u; PageTable[index] = 0u; }
    if (index < 4u) RequestCounts[index] = 0u;
    if (index < physicalCapacity)
    {
        PhysicalPage page = PhysicalPages[index];
        if (resetCache != 0u)
        {
            page.key0 = 0xFFFFFFFFu;
            page.key1 = 0xFFFFFFFFu;
            page.lastUsedFrame = 0u;
            page.flags = 0u;
        }
        else if ((page.flags & 1u) != 0u)
        {
            bool invalidated = invalidationCount == 0xFFFFFFFFu;
            [loop]
            for (uint invalidationIndex = 0u; !invalidated && invalidationIndex < min(invalidationCount, 2048u); ++invalidationIndex)
            {
                invalidated = invalidatedPages[invalidationIndex].data.x == page.key0 &&
                              (invalidatedPages[invalidationIndex].data.y & 0xFFu) == (page.key1 & 0xFFu);
            }
            if (invalidated) page.flags |= 2u;
        }
        page.flags &= ~4u;
        PhysicalPages[index] = page;
    }
}
)";

        inline const std::string MarkComputeHlsl = std::string(CommonConstants) + R"(
Texture2D<float> SceneDepth : register(t0);
RWStructuredBuffer<uint> PageMarks : register(u0);
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
    uint firstLevel = clipmapCount - 1u;
    [unroll]
    for (uint levelIndex = 0u; levelIndex < 4u; ++levelIndex)
    {
        if (levelIndex < clipmapCount && cameraDepth <= clipmaps[levelIndex].radiusAndDepth.x)
        {
            firstLevel = levelIndex;
            break;
        }
    }
    [loop]
    for (uint level = firstLevel; level < clipmapCount; ++level)
    {
        float pageSize = clipmaps[level].originAndPageSize.w;
        int2 page = int2(floor(lightPosition / pageSize));
        int2 minimumPage = clipmaps[level].pageData.xy - int2(64, 64);
        int2 localPage = page - minimumPage;
        if (all(localPage >= 0) && all(localPage < 128))
        {
            uint logical = level * 16384u + uint(localPage.y) * 128u + uint(localPage.x);
            InterlockedOr(PageMarks[logical], 1u);
        }
    }
}
)";

        inline const std::string CompactRequestsComputeHlsl = std::string(CommonConstants) + R"(
StructuredBuffer<uint> PageMarks : register(t0);
RWStructuredBuffer<uint> RequestList : register(u0);
RWStructuredBuffer<uint> RequestCounts : register(u1);
[numthreads(64, 1, 1)]
void CSMain(uint logical : SV_DispatchThreadID)
{
    if (logical >= 65536u) return;
    uint level = logical / 16384u;
    uint levelIndex = logical - level * 16384u;
    int2 localPage = int2(levelIndex & 127u, levelIndex >> 7u);
    bool requested = false;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            int2 neighbor = localPage + int2(x, y);
            if (all(neighbor >= 0) && all(neighbor < 128))
            {
                uint neighborLogical = level * 16384u + uint(neighbor.y) * 128u + uint(neighbor.x);
                requested = requested || PageMarks[neighborLogical] != 0u;
            }
        }
    }
    if (!requested) return;
    uint requestIndex = 0u;
    InterlockedAdd(RequestCounts[level], 1u, requestIndex);
    RequestList[level * 16384u + requestIndex] = logical;
}
)";

        inline const std::string ResolvePageHitsComputeHlsl = std::string(CommonConstants) + R"(
StructuredBuffer<uint> RequestList : register(t0);
StructuredBuffer<uint> RequestCounts : register(t1);
RWStructuredBuffer<uint> PageTable : register(u0);
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u1);
[numthreads(64, 1, 1)]
void CSMain(uint requestAddress : SV_DispatchThreadID)
{
    if (passLevel >= clipmapCount || requestAddress >= 16384u) return;
    uint level = passLevel;
    uint requestIndex = requestAddress;
    if (requestIndex >= min(RequestCounts[level], 16384u)) return;

    uint logical = RequestList[level * 16384u + requestIndex];
    uint levelIndex = logical - level * 16384u;
    int2 localPage = int2(levelIndex & 127u, levelIndex >> 7u);
    int2 absolutePage = clipmaps[level].pageData.xy - int2(64, 64) + localPage;
    uint key0 = (uint(absolutePage.x) & 0xFFFFu) | ((uint(absolutePage.y) & 0xFFFFu) << 16u);
    uint key1 = level | ((uint(clipmaps[level].pageData.z) & 0x00FFFFFFu) << 8u);

    for (uint physical = 0u; physical < physicalCapacity; ++physical)
    {
        PhysicalPage page = PhysicalPages[physical];
        if (page.key0 == key0 && page.key1 == key1)
        {
            InterlockedMax(PhysicalPages[physical].lastUsedFrame, frameIndex);
            InterlockedOr(PhysicalPages[physical].flags, 4u);
            PageTable[logical] = physical + 1u;
            return;
        }
    }
}
)";

        inline const std::string AllocateComputeHlsl = std::string(CommonConstants) + R"(
StructuredBuffer<uint> RequestList : register(t0);
StructuredBuffer<uint> RequestCounts : register(t1);
RWStructuredBuffer<uint> PageTable : register(u0);
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u1);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    if (passLevel >= clipmapCount) return;
    uint pinnedCount = 0u;
    for (uint physical = 0u; physical < physicalCapacity; ++physical)
    {
        pinnedCount += (PhysicalPages[physical].flags & 4u) != 0u ? 1u : 0u;
    }

    uint level = passLevel;
    uint requestCount = min(RequestCounts[level], 16384u);
    for (uint requestIndex = 0u; requestIndex < requestCount && pinnedCount < physicalCapacity; ++requestIndex)
    {
        uint logical = RequestList[level * 16384u + requestIndex];
        if (PageTable[logical] != 0u) continue;
        uint levelIndex = logical - level * 16384u;
        int2 localPage = int2(levelIndex & 127u, levelIndex >> 7u);
        int2 absolutePage = clipmaps[level].pageData.xy - int2(64, 64) + localPage;
        uint key0 = (uint(absolutePage.x) & 0xFFFFu) | ((uint(absolutePage.y) & 0xFFFFu) << 16u);
        uint key1 = level | ((uint(clipmaps[level].pageData.z) & 0x00FFFFFFu) << 8u);

        uint selected = 0xFFFFFFFFu;
        uint oldestFrame = 0xFFFFFFFFu;
        uint oldestPage = 0xFFFFFFFFu;
        for (uint physical = 0u; physical < physicalCapacity; ++physical)
        {
            PhysicalPage page = PhysicalPages[physical];
            if ((page.flags & 1u) != 0u && page.key0 == key0 && page.key1 == key1) { selected = physical; break; }
            if ((page.flags & 1u) == 0u && selected == 0xFFFFFFFFu) selected = physical;
            if ((page.flags & 4u) == 0u && page.lastUsedFrame < oldestFrame) { oldestFrame = page.lastUsedFrame; oldestPage = physical; }
        }
        if (selected == 0xFFFFFFFFu) selected = oldestPage;
        if (selected == 0xFFFFFFFFu) continue;

        PhysicalPage selectedPage = PhysicalPages[selected];
        bool cacheHit = (selectedPage.flags & 1u) != 0u && selectedPage.key0 == key0 && selectedPage.key1 == key1;
        bool wasRequested = (selectedPage.flags & 4u) != 0u;
        selectedPage.key0 = key0;
        selectedPage.key1 = key1;
        selectedPage.lastUsedFrame = frameIndex;
        selectedPage.flags = 1u | 4u | (cacheHit ? (selectedPage.flags & 2u) : 2u);
        PhysicalPages[selected] = selectedPage;
        PageTable[logical] = selected + 1u;
        if (!wasRequested) ++pinnedCount;
    }
}
)";

        inline const std::string FinalizeComputeHlsl = std::string(CommonConstants) + R"(
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u0);
[numthreads(64, 1, 1)]
void CSMain(uint index : SV_DispatchThreadID)
{
    if (index < physicalCapacity)
    {
        PhysicalPage page = PhysicalPages[index];
        if ((page.flags & 4u) != 0u) page.flags &= ~6u;
        PhysicalPages[index] = page;
    }
}
)";

        inline const std::string PageClearHlsl = std::string(CommonConstants) + R"(
StructuredBuffer<PhysicalPage> PhysicalPages : register(t2);
struct Output { float4 position : SV_POSITION; };
Output VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    Output output;
    PhysicalPage page = PhysicalPages[instanceID];
    if ((page.flags & 6u) != 6u) { output.position = float4(2.0f, 2.0f, 1.0f, 1.0f); return output; }
    const float2 vertices[6] = {
        float2(0.0f, 0.0f), float2(0.0f, 1.0f), float2(1.0f, 0.0f),
        float2(1.0f, 0.0f), float2(0.0f, 1.0f), float2(1.0f, 1.0f)
    };
    uint pagesPerRow = atlasExtent / physicalPageSize;
    float2 slot = float2(instanceID % pagesPerRow, instanceID / pagesPerRow) * physicalPageSize;
    float2 pixel = slot + vertices[vertexID] * physicalPageSize;
    output.position = float4(pixel.x / atlasExtent * 2.0f - 1.0f, 1.0f - pixel.y / atlasExtent * 2.0f, 1.0f, 1.0f);
    return output;
}
)";

        inline const std::string PageCasterHlsl = std::string(CommonConstants) + R"(
cbuffer ObjectConstants : register(b2) { float4x4 localToWorld; uint receiveShadows; uint3 objectPadding; };
StructuredBuffer<PhysicalPage> PhysicalPages : register(t2);
struct Input { float3 position : POSITION; };
struct Output { float4 position : SV_POSITION; float4 pageClip : SV_ClipDistance0; };
Output VSMain(Input input, uint instanceID : SV_InstanceID)
{
    Output output;
    PhysicalPage page = PhysicalPages[instanceID];
    if ((page.flags & 6u) != 6u)
    {
        output.position = float4(2.0f, 2.0f, 1.0f, 1.0f);
        output.pageClip = -1.0f;
        return output;
    }
    int2 pageCoordinate = int2(int(page.key0 << 16u) >> 16, int(page.key0) >> 16);
    uint level = page.key1 & 0xFFu;
    float4 world = mul(localToWorld, float4(input.position, 1.0f));
    float3 light = float3(dot(world.xyz, lightRight.xyz), dot(world.xyz, lightUp.xyz), dot(world.xyz, lightForward.xyz));
    float pageSize = clipmaps[level].originAndPageSize.w;
    float2 pageUv = light.xy / pageSize - float2(pageCoordinate);
    const float gutterUv = 1.0f / 126.0f;
    output.pageClip = float4(pageUv.x + gutterUv, 1.0f + gutterUv - pageUv.x,
                             pageUv.y + gutterUv, 1.0f + gutterUv - pageUv.y);
    uint pagesPerRow = atlasExtent / physicalPageSize;
    float2 slot = float2(instanceID % pagesPerRow, instanceID / pagesPerRow) * physicalPageSize;
    float2 pixel = slot + 1.0f + float2(pageUv.x, 1.0f - pageUv.y) * 126.0f;
    float depth = (light.z - clipmaps[level].radiusAndDepth.y) /
                  (clipmaps[level].radiusAndDepth.z - clipmaps[level].radiusAndDepth.y);
    output.position = float4(pixel.x / atlasExtent * 2.0f - 1.0f, 1.0f - pixel.y / atlasExtent * 2.0f, depth, 1.0f);
    return output;
}
)";

        struct DepthData
        {
            FrameGraphTextureHandle depth;
        };
        struct ClearData
        {
            FrameGraphBufferHandle marks;
            FrameGraphBufferHandle table;
            FrameGraphBufferHandle requestCounts;
            FrameGraphBufferHandle physical;
        };
        struct MarkData
        {
            FrameGraphTextureHandle depth;
            FrameGraphBufferHandle marks;
        };
        struct CompactData
        {
            FrameGraphBufferHandle marks;
            FrameGraphBufferHandle requestList;
            FrameGraphBufferHandle requestCounts;
        };
        struct ResolveData
        {
            FrameGraphBufferHandle requestList;
            FrameGraphBufferHandle requestCounts;
            FrameGraphBufferHandle table;
            FrameGraphBufferHandle physical;
        };
        struct AllocateData
        {
            FrameGraphBufferHandle requestList;
            FrameGraphBufferHandle requestCounts;
            FrameGraphBufferHandle table;
            FrameGraphBufferHandle physical;
        };
        struct DrawData
        {
            FrameGraphTextureHandle atlas;
            FrameGraphBufferHandle physical;
        };
        struct FinalizeData
        {
            FrameGraphBufferHandle physical;
        };

        UniformBufferAllocation UploadConstants(RenderPassContext& context, UInt32 passLevel = 0)
        {
            VirtualShadowGpuConstants constants = BuildVirtualShadowGpuConstants(*context.rendererData.virtualShadowPacket);
            constants.passLevel = passLevel;
            return context.frameData.UploadUniform(&constants, sizeof(constants));
        }

        [[nodiscard]] bool IsGpuVirtualShadowEnabled(const RenderPassContext& context) noexcept
        {
            return context.rendererData.virtualShadowPacket != nullptr && context.rendererData.virtualShadowPacket->enabled &&
                   context.rendererData.virtualShadowPacket->gpuDriven;
        }

        ErrorCode DisableGpuVirtualShadows(RenderPassContext& context, const char* stage)
        {
            VE_LOG_ERROR("GPU-driven virtual shadows failed during %s; disabling the GPU path and using the CPU fallback on subsequent frames.", stage);
            if (context.rendererData.viewState != nullptr)
            {
                context.rendererData.viewState->GetVirtualShadowViewCache().DisableGpuDriven();
            }
            if (context.rendererData.virtualShadowPacket != nullptr)
            {
                context.rendererData.virtualShadowPacket->enabled = false;
                context.rendererData.virtualShadowPacket->gpuDriven = false;
            }
            return ErrorCode::None;
        }
    } // namespace

    void GpuVirtualShadowRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        const auto& packet = frameGraph.GetRendererData().virtualShadowPacket;
        if (packet == nullptr || !packet->enabled || !packet->gpuDriven || !graphData.depth.IsValid() || !graphData.virtualShadowAtlas.IsValid())
        {
            return;
        }

        frameGraph.AddRasterPass<DepthData>(
            "VirtualShadowReceiverDepth",
            [&graphData](FrameGraphBuilder& builder, DepthData& data)
            {
                data.depth = builder.WriteDepthAttachment(graphData.depth, rhi::RhiLoadAction::Clear);
                graphData.depth = data.depth;
                graphData.depthPrepassReady = true;
            },
            [this](const DepthData&, RenderPassContext& context) { return DrawReceiverDepth(context); });

        frameGraph.AddComputePass<ClearData>(
            "VirtualShadowClearRequests",
            [&graphData](FrameGraphBuilder& builder, ClearData& data)
            {
                data.marks = builder.Write(graphData.virtualShadowPageMarks);
                data.table = builder.Write(graphData.virtualShadowPageTable);
                data.requestCounts = builder.Write(graphData.virtualShadowRequestCounts);
                data.physical = builder.Write(graphData.virtualShadowPhysicalPages);
                graphData.virtualShadowPageMarks = data.marks;
                graphData.virtualShadowPageTable = data.table;
                graphData.virtualShadowRequestCounts = data.requestCounts;
                graphData.virtualShadowPhysicalPages = data.physical;
            },
            [this](const ClearData& data, const FrameGraphPassResources& resources, RenderPassContext& context)
            { return DispatchClear(resources, data.marks, data.table, data.requestCounts, data.physical, context); });

        frameGraph.AddComputePass<MarkData>(
            "VirtualShadowMarkPages",
            [&graphData](FrameGraphBuilder& builder, MarkData& data)
            {
                data.depth = builder.Read(graphData.depth);
                data.marks = builder.Write(graphData.virtualShadowPageMarks);
                graphData.virtualShadowPageMarks = data.marks;
            },
            [this](const MarkData& data, const FrameGraphPassResources& resources, RenderPassContext& context)
            { return DispatchMark(resources, data.depth, data.marks, context); });

        frameGraph.AddComputePass<CompactData>(
            "VirtualShadowCompactRequests",
            [&graphData](FrameGraphBuilder& builder, CompactData& data)
            {
                data.marks = builder.Read(graphData.virtualShadowPageMarks);
                data.requestList = builder.Write(graphData.virtualShadowRequestList);
                data.requestCounts = builder.Write(graphData.virtualShadowRequestCounts);
                graphData.virtualShadowRequestList = data.requestList;
                graphData.virtualShadowRequestCounts = data.requestCounts;
            },
            [this](const CompactData& data, const FrameGraphPassResources& resources, RenderPassContext& context)
            { return DispatchCompact(resources, data.marks, data.requestList, data.requestCounts, context); });

        for (UInt32 coarseIteration = 0; coarseIteration < VirtualShadowClipmapLevelCount; ++coarseIteration)
        {
            const UInt32 level = VirtualShadowClipmapLevelCount - 1u - coarseIteration;
            frameGraph.AddComputePass<ResolveData>(
                "VirtualShadowResolvePageHitsL" + std::to_string(level),
                [&graphData](FrameGraphBuilder& builder, ResolveData& data)
                {
                    data.requestList = builder.Read(graphData.virtualShadowRequestList);
                    data.requestCounts = builder.Read(graphData.virtualShadowRequestCounts);
                    data.table = builder.Write(graphData.virtualShadowPageTable);
                    data.physical = builder.Write(graphData.virtualShadowPhysicalPages);
                    graphData.virtualShadowPageTable = data.table;
                    graphData.virtualShadowPhysicalPages = data.physical;
                },
                [this, level](const ResolveData& data, const FrameGraphPassResources& resources, RenderPassContext& context)
                { return DispatchResolvePageHits(resources, data.requestList, data.requestCounts, data.table, data.physical, level, context); });

            frameGraph.AddComputePass<AllocateData>(
                "VirtualShadowAllocatePagesL" + std::to_string(level),
                [&graphData](FrameGraphBuilder& builder, AllocateData& data)
                {
                    data.requestList = builder.Read(graphData.virtualShadowRequestList);
                    data.requestCounts = builder.Read(graphData.virtualShadowRequestCounts);
                    data.table = builder.Write(graphData.virtualShadowPageTable);
                    data.physical = builder.Write(graphData.virtualShadowPhysicalPages);
                    graphData.virtualShadowPageTable = data.table;
                    graphData.virtualShadowPhysicalPages = data.physical;
                },
                [this, level](const AllocateData& data, const FrameGraphPassResources& resources, RenderPassContext& context)
                { return DispatchAllocate(resources, data.requestList, data.requestCounts, data.table, data.physical, level, context); });
        }

        frameGraph.AddRasterPass<DrawData>(
            "VirtualShadowRenderPages",
            [&graphData, extent = packet->atlasExtent](FrameGraphBuilder& builder, DrawData& data)
            {
                data.atlas = builder.WriteDepthAttachment(graphData.virtualShadowAtlas, rhi::RhiLoadAction::Load);
                data.physical = builder.Read(graphData.virtualShadowPhysicalPages);
                graphData.virtualShadowAtlas = data.atlas;
                builder.SetRenderArea({0, 0, extent, extent});
                builder.SetViewport({0.0f, 0.0f, static_cast<Float32>(extent), static_cast<Float32>(extent), 0.0f, 1.0f});
                builder.SetScissor({0, 0, extent, extent});
            },
            [this](const DrawData& data, const FrameGraphPassResources& resources, RenderPassContext& context)
            { return DrawPages(resources, data.physical, context); });

        frameGraph.AddComputePass<FinalizeData>(
            "VirtualShadowFinalizePages",
            [&graphData](FrameGraphBuilder& builder, FinalizeData& data)
            {
                data.physical = builder.Write(graphData.virtualShadowPhysicalPages);
                graphData.virtualShadowPhysicalPages = data.physical;
                builder.SetSideEffect();
            },
            [this](const FinalizeData& data, const FrameGraphPassResources& resources, RenderPassContext& context)
            { return DispatchFinalize(resources, data.physical, context); });
    }

    ErrorCode GpuVirtualShadowRenderPass::DrawReceiverDepth(RenderPassContext& context)
    {
        if (!IsGpuVirtualShadowEnabled(context))
        {
            return ErrorCode::None;
        }
        const auto view = context.frameData.GetViewUniform(context.rendererData.resolvedCamera.get(),
                                                           {context.executionInfo.renderArea.width, context.executionInfo.renderArea.height});
        for (const auto& item : context.rendererData.opaqueItems)
        {
            if (item == nullptr)
            {
                continue;
            }
            const auto mesh = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
            const auto material = std::dynamic_pointer_cast<RTMaterialResource>(item->GetMaterialResource());
            if (mesh == nullptr || mesh->GetVertexBuffer() == nullptr || material == nullptr || material->GetShaderResource() == nullptr)
            {
                continue;
            }
            rhi::RhiPipelineState* pipeline = GetReceiverDepthPipeline(context, *material->GetShaderResource());
            if (pipeline == nullptr)
            {
                return DisableGpuVirtualShadows(context, "receiver-depth pipeline creation");
            }
            context.commandList.SetPipeline(*pipeline);
            context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *view.buffer, view.offset, view.size);
            const auto object = context.frameData.GetObjectUniform(*item);
            context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 2, *object.buffer, object.offset, object.size);
            context.commandList.SetVertexBuffer(0, *mesh->GetVertexBuffer(), mesh->GetVertexStride(), 0);
            if (mesh->GetIndexBuffer() != nullptr && mesh->GetIndexCount() != 0)
            {
                context.commandList.SetIndexBuffer(*mesh->GetIndexBuffer(), rhi::RhiIndexFormat::UInt32, 0);
                context.commandList.DrawIndexed(mesh->GetIndexCount(), 0, 0);
            }
            else
            {
                context.commandList.Draw(mesh->GetVertexCount(), 0);
            }
        }
        return ErrorCode::None;
    }

    ErrorCode GpuVirtualShadowRenderPass::DispatchClear(const FrameGraphPassResources& resources,
                                                        FrameGraphBufferHandle pageMarks,
                                                        FrameGraphBufferHandle pageTable,
                                                        FrameGraphBufferHandle requestCounts,
                                                        FrameGraphBufferHandle physicalPages,
                                                        RenderPassContext& context)
    {
        if (!IsGpuVirtualShadowEnabled(context))
        {
            return ErrorCode::None;
        }
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 2},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 3},
        };
        rhi::RhiPipelineResourceLayoutDesc layout{bindings, static_cast<UInt32>(std::size(bindings))};
        auto* pipeline = GetComputePipeline(context, "VirtualShadowClear", ShaderID{"VirtualShadow.Clear.Compute", 0}, ClearComputeHlsl.c_str(), layout);
        const auto marks = resources.GetBuffer(pageMarks);
        const auto table = resources.GetBuffer(pageTable);
        const auto counts = resources.GetBuffer(requestCounts);
        const auto physical = resources.GetBuffer(physicalPages);
        if (pipeline == nullptr || marks.buffer == nullptr || table.buffer == nullptr || counts.buffer == nullptr || physical.buffer == nullptr)
        {
            return DisableGpuVirtualShadows(context, "request-clear setup");
        }
        const auto constants = UploadConstants(context);
        context.commandList.SetComputePipeline(*pipeline);
        context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, *marks.buffer, 0, marks.buffer->GetSize());
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 1, *table.buffer, 0, table.buffer->GetSize());
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 2, *counts.buffer, 0, counts.buffer->GetSize());
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 3, *physical.buffer, 0, physical.buffer->GetSize());
        context.commandList.Dispatch((VirtualShadowLogicalPageCount + 63u) / 64u, 1, 1);
        return ErrorCode::None;
    }

    ErrorCode GpuVirtualShadowRenderPass::DispatchMark(const FrameGraphPassResources& resources,
                                                       FrameGraphTextureHandle sceneDepth,
                                                       FrameGraphBufferHandle pageMarks,
                                                       RenderPassContext& context)
    {
        if (!IsGpuVirtualShadowEnabled(context))
        {
            return ErrorCode::None;
        }
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
            {rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Compute, 0},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
        };
        rhi::RhiPipelineResourceLayoutDesc layout{bindings, static_cast<UInt32>(std::size(bindings))};
        auto* pipeline = GetComputePipeline(context, "VirtualShadowMark", ShaderID{"VirtualShadow.Mark.Compute", 0}, MarkComputeHlsl.c_str(), layout);
        const auto depth = resources.GetTexture(sceneDepth);
        const auto marks = resources.GetBuffer(pageMarks);
        const auto& packet = *context.rendererData.virtualShadowPacket;
        if (pipeline == nullptr || depth.texture == nullptr || marks.buffer == nullptr)
        {
            return DisableGpuVirtualShadows(context, "receiver-page marking setup");
        }
        const auto constants = UploadConstants(context);
        context.commandList.SetComputePipeline(*pipeline);
        context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
        context.commandList.SetTexture(rhi::RhiShaderStage::Compute, 0, *depth.texture);
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, *marks.buffer, 0, marks.buffer->GetSize());
        context.commandList.Dispatch((packet.screenWidth + 7u) / 8u, (packet.screenHeight + 7u) / 8u, 1);
        return ErrorCode::None;
    }

    ErrorCode GpuVirtualShadowRenderPass::DispatchCompact(const FrameGraphPassResources& resources,
                                                          FrameGraphBufferHandle pageMarks,
                                                          FrameGraphBufferHandle requestList,
                                                          FrameGraphBufferHandle requestCounts,
                                                          RenderPassContext& context)
    {
        if (!IsGpuVirtualShadowEnabled(context))
        {
            return ErrorCode::None;
        }
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Compute, 0},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
        };
        rhi::RhiPipelineResourceLayoutDesc layout{bindings, static_cast<UInt32>(std::size(bindings))};
        auto* pipeline = GetComputePipeline(
            context, "VirtualShadowCompactRequests", ShaderID{"VirtualShadow.CompactRequests.Compute", 0}, CompactRequestsComputeHlsl.c_str(), layout);
        const auto marks = resources.GetBuffer(pageMarks);
        const auto requests = resources.GetBuffer(requestList);
        const auto counts = resources.GetBuffer(requestCounts);
        if (pipeline == nullptr || marks.buffer == nullptr || requests.buffer == nullptr || counts.buffer == nullptr)
        {
            return DisableGpuVirtualShadows(context, "request compaction setup");
        }
        const auto constants = UploadConstants(context);
        context.commandList.SetComputePipeline(*pipeline);
        context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
        context.commandList.SetStorageBuffer(rhi::RhiShaderStage::Compute, 0, *marks.buffer, 0, marks.buffer->GetSize());
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, *requests.buffer, 0, requests.buffer->GetSize());
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 1, *counts.buffer, 0, counts.buffer->GetSize());
        context.commandList.Dispatch((VirtualShadowLogicalPageCount + 63u) / 64u, 1, 1);
        return ErrorCode::None;
    }

    ErrorCode GpuVirtualShadowRenderPass::DispatchResolvePageHits(const FrameGraphPassResources& resources,
                                                                  FrameGraphBufferHandle requestList,
                                                                  FrameGraphBufferHandle requestCounts,
                                                                  FrameGraphBufferHandle pageTable,
                                                                  FrameGraphBufferHandle physicalPages,
                                                                  UInt32 level,
                                                                  RenderPassContext& context)
    {
        if (!IsGpuVirtualShadowEnabled(context))
        {
            return ErrorCode::None;
        }
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Compute, 0},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Compute, 1},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
        };
        rhi::RhiPipelineResourceLayoutDesc layout{bindings, static_cast<UInt32>(std::size(bindings))};
        auto* pipeline = GetComputePipeline(
            context, "VirtualShadowResolvePageHits", ShaderID{"VirtualShadow.ResolvePageHits.Compute", 0}, ResolvePageHitsComputeHlsl.c_str(), layout);
        const auto requests = resources.GetBuffer(requestList);
        const auto counts = resources.GetBuffer(requestCounts);
        const auto table = resources.GetBuffer(pageTable);
        const auto physical = resources.GetBuffer(physicalPages);
        if (pipeline == nullptr || requests.buffer == nullptr || counts.buffer == nullptr || table.buffer == nullptr || physical.buffer == nullptr)
        {
            return DisableGpuVirtualShadows(context, "page cache-hit resolution setup");
        }
        const auto constants = UploadConstants(context, level);
        context.commandList.SetComputePipeline(*pipeline);
        context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
        context.commandList.SetStorageBuffer(rhi::RhiShaderStage::Compute, 0, *requests.buffer, 0, requests.buffer->GetSize());
        context.commandList.SetStorageBuffer(rhi::RhiShaderStage::Compute, 1, *counts.buffer, 0, counts.buffer->GetSize());
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, *table.buffer, 0, table.buffer->GetSize());
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 1, *physical.buffer, 0, physical.buffer->GetSize());
        context.commandList.Dispatch((VirtualShadowPagesPerAxis * VirtualShadowPagesPerAxis + 63u) / 64u, 1, 1);
        return ErrorCode::None;
    }

    ErrorCode GpuVirtualShadowRenderPass::DispatchAllocate(const FrameGraphPassResources& resources,
                                                           FrameGraphBufferHandle requestList,
                                                           FrameGraphBufferHandle requestCounts,
                                                           FrameGraphBufferHandle pageTable,
                                                           FrameGraphBufferHandle physicalPages,
                                                           UInt32 level,
                                                           RenderPassContext& context)
    {
        if (!IsGpuVirtualShadowEnabled(context))
        {
            return ErrorCode::None;
        }
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Compute, 0},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Compute, 1},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
        };
        rhi::RhiPipelineResourceLayoutDesc layout{bindings, static_cast<UInt32>(std::size(bindings))};
        auto* pipeline =
            GetComputePipeline(context, "VirtualShadowAllocate", ShaderID{"VirtualShadow.Allocate.Compute", 0}, AllocateComputeHlsl.c_str(), layout);
        const auto requests = resources.GetBuffer(requestList);
        const auto counts = resources.GetBuffer(requestCounts);
        const auto table = resources.GetBuffer(pageTable);
        const auto physical = resources.GetBuffer(physicalPages);
        if (pipeline == nullptr || requests.buffer == nullptr || counts.buffer == nullptr || table.buffer == nullptr || physical.buffer == nullptr)
        {
            return DisableGpuVirtualShadows(context, "page allocation setup");
        }
        const auto constants = UploadConstants(context, level);
        context.commandList.SetComputePipeline(*pipeline);
        context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
        context.commandList.SetStorageBuffer(rhi::RhiShaderStage::Compute, 0, *requests.buffer, 0, requests.buffer->GetSize());
        context.commandList.SetStorageBuffer(rhi::RhiShaderStage::Compute, 1, *counts.buffer, 0, counts.buffer->GetSize());
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, *table.buffer, 0, table.buffer->GetSize());
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 1, *physical.buffer, 0, physical.buffer->GetSize());
        context.commandList.Dispatch(1, 1, 1);
        return ErrorCode::None;
    }

    ErrorCode GpuVirtualShadowRenderPass::DrawPages(const FrameGraphPassResources& resources, FrameGraphBufferHandle physicalPages, RenderPassContext& context)
    {
        if (!IsGpuVirtualShadowEnabled(context))
        {
            return ErrorCode::None;
        }
        const auto physical = resources.GetBuffer(physicalPages);
        const UInt32 capacity = context.rendererData.viewState->GetVirtualShadowViewCache().GetGpuPhysicalPageCapacity();
        if (physical.buffer == nullptr || capacity == 0)
        {
            return DisableGpuVirtualShadows(context, "physical-page rendering setup");
        }
        const auto constants = UploadConstants(context);
        rhi::RhiPipelineState* clearPipeline = GetPageClearPipeline(context);
        rhi::RhiPipelineState* casterPipeline = GetPageCasterPipeline(context);
        if (clearPipeline == nullptr || casterPipeline == nullptr)
        {
            return DisableGpuVirtualShadows(context, "physical-page pipeline creation");
        }
        context.commandList.SetPipeline(*clearPipeline);
        context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 4, *constants.buffer, constants.offset, constants.size);
        context.commandList.SetStorageBuffer(rhi::RhiShaderStage::Vertex, 2, *physical.buffer, 0, physical.buffer->GetSize());
        context.commandList.DrawInstanced(VirtualShadowPageClearVertexCount, capacity, 0, 0);
        for (const auto& item : context.rendererData.opaqueItems)
        {
            if (item == nullptr || !item->CastShadows())
            {
                continue;
            }
            const auto mesh = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
            if (mesh == nullptr || mesh->GetVertexBuffer() == nullptr)
            {
                continue;
            }
            context.commandList.SetPipeline(*casterPipeline);
            context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 4, *constants.buffer, constants.offset, constants.size);
            context.commandList.SetStorageBuffer(rhi::RhiShaderStage::Vertex, 2, *physical.buffer, 0, physical.buffer->GetSize());
            const auto object = context.frameData.GetObjectUniform(*item);
            context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 2, *object.buffer, object.offset, object.size);
            context.commandList.SetVertexBuffer(0, *mesh->GetVertexBuffer(), mesh->GetVertexStride(), 0);
            if (mesh->GetIndexBuffer() != nullptr && mesh->GetIndexCount() != 0)
            {
                context.commandList.SetIndexBuffer(*mesh->GetIndexBuffer(), rhi::RhiIndexFormat::UInt32, 0);
                context.commandList.DrawIndexedInstanced(mesh->GetIndexCount(), capacity, 0, 0, 0);
            }
            else
            {
                context.commandList.DrawInstanced(mesh->GetVertexCount(), capacity, 0, 0);
            }
        }
        return ErrorCode::None;
    }

    ErrorCode
    GpuVirtualShadowRenderPass::DispatchFinalize(const FrameGraphPassResources& resources, FrameGraphBufferHandle physicalPages, RenderPassContext& context)
    {
        if (!IsGpuVirtualShadowEnabled(context))
        {
            return ErrorCode::None;
        }
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
            {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
        };
        rhi::RhiPipelineResourceLayoutDesc layout{bindings, static_cast<UInt32>(std::size(bindings))};
        auto* pipeline =
            GetComputePipeline(context, "VirtualShadowFinalize", ShaderID{"VirtualShadow.Finalize.Compute", 0}, FinalizeComputeHlsl.c_str(), layout);
        const auto physical = resources.GetBuffer(physicalPages);
        if (pipeline == nullptr || physical.buffer == nullptr)
        {
            return DisableGpuVirtualShadows(context, "page finalization setup");
        }
        const auto constants = UploadConstants(context);
        context.commandList.SetComputePipeline(*pipeline);
        context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
        context.commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, *physical.buffer, 0, physical.buffer->GetSize());
        context.commandList.Dispatch((context.rendererData.viewState->GetVirtualShadowViewCache().GetGpuPhysicalPageCapacity() + 63u) / 64u, 1, 1);
        return ErrorCode::None;
    }

    rhi::RhiPipelineState* GpuVirtualShadowRenderPass::GetReceiverDepthPipeline(RenderPassContext& context, RTShaderResource& shaderResource)
    {
        rhi::RhiShaderModule* vertexShader = shaderResource.GetVertexShader();
        if (vertexShader == nullptr)
        {
            return nullptr;
        }
        rhi::RhiVertexAttributeDesc attributes[] = {{"POSITION", 0, rhi::RhiFormat::Rgb32Float, 0},
                                                    {"NORMAL", 0, rhi::RhiFormat::Rgb32Float, sizeof(Float32) * 3}};
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 1},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 2},
        };
        rhi::RhiGraphicsPipelineDesc desc = {};
        desc.blendState = rhi::StaticRenderStates::OpaqueBlend;
        desc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
        desc.depthStencilState = rhi::StaticRenderStates::DepthReadWriteLessEqual;
        desc.boundShaderState.vertexShader = vertexShader;
        desc.boundShaderState.vertexDeclaration = {attributes, static_cast<UInt32>(std::size(attributes)), sizeof(RTMeshVertex)};
        desc.resourceLayout = {bindings, static_cast<UInt32>(std::size(bindings))};
        desc.colorAttachmentCount = 0;
        desc.colorFormat = rhi::RhiFormat::Unknown;
        desc.depthFormat = rhi::RhiFormat::Depth32Float;
        const std::string name = "VirtualShadowReceiverDepth:" + shaderResource.GetDesc().name;
        return context.frameData.shaderManager->GetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{name, static_cast<Int32>(reinterpret_cast<std::uintptr_t>(vertexShader))}, desc);
    }

    rhi::RhiPipelineState* GpuVirtualShadowRenderPass::GetPageClearPipeline(RenderPassContext& context)
    {
        rhi::RhiShaderModuleDesc shaderDesc = {
            rhi::RhiShaderStage::Vertex, rhi::RhiShaderCodeFormat::Source, PageClearHlsl.c_str(), nullptr, 0, "VSMain", "VirtualShadowPageClear"};
        auto* shader = context.frameData.shaderManager->GetOrCompileShader(context.device, ShaderID{"VirtualShadow.PageClear.Vertex", 0}, shaderDesc);
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 4},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Vertex, 2},
        };
        rhi::RhiGraphicsPipelineDesc desc = {};
        desc.blendState = rhi::StaticRenderStates::OpaqueBlend;
        desc.rasterizerState = rhi::StaticRenderStates::SolidNoCullRasterizer;
        desc.depthStencilState = rhi::StaticRenderStates::DepthReadWriteAlways;
        desc.boundShaderState.vertexShader = shader;
        desc.resourceLayout = {bindings, static_cast<UInt32>(std::size(bindings))};
        desc.colorAttachmentCount = 0;
        desc.colorFormat = rhi::RhiFormat::Unknown;
        desc.depthFormat = rhi::RhiFormat::Depth32Float;
        return shader == nullptr
                   ? nullptr
                   : context.frameData.shaderManager->GetOrCreateGraphicsPipeline(context.device, GraphicsPipelineID{"VirtualShadowPageClear", 0}, desc);
    }

    rhi::RhiPipelineState* GpuVirtualShadowRenderPass::GetPageCasterPipeline(RenderPassContext& context)
    {
        rhi::RhiShaderModuleDesc shaderDesc = {
            rhi::RhiShaderStage::Vertex, rhi::RhiShaderCodeFormat::Source, PageCasterHlsl.c_str(), nullptr, 0, "VSMain", "VirtualShadowPageCaster"};
        auto* shader = context.frameData.shaderManager->GetOrCompileShader(context.device, ShaderID{"VirtualShadow.PageCaster.Vertex", 0}, shaderDesc);
        rhi::RhiVertexAttributeDesc attribute = {"POSITION", 0, rhi::RhiFormat::Rgb32Float, 0};
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 2},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 4},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Vertex, 2},
        };
        rhi::RhiGraphicsPipelineDesc desc = {};
        desc.blendState = rhi::StaticRenderStates::OpaqueBlend;
        desc.rasterizerState = rhi::StaticRenderStates::SolidNoCullRasterizer;
        desc.depthStencilState = rhi::StaticRenderStates::DepthReadWriteLessEqual;
        desc.boundShaderState.vertexShader = shader;
        desc.boundShaderState.vertexDeclaration = {&attribute, 1, sizeof(RTMeshVertex)};
        desc.resourceLayout = {bindings, static_cast<UInt32>(std::size(bindings))};
        desc.colorAttachmentCount = 0;
        desc.colorFormat = rhi::RhiFormat::Unknown;
        desc.depthFormat = rhi::RhiFormat::Depth32Float;
        return shader == nullptr
                   ? nullptr
                   : context.frameData.shaderManager->GetOrCreateGraphicsPipeline(context.device, GraphicsPipelineID{"VirtualShadowPageCaster", 0}, desc);
    }

    rhi::RhiComputePipelineState* GpuVirtualShadowRenderPass::GetComputePipeline(
        RenderPassContext& context, const char* name, ShaderID shaderId, const char* source, const rhi::RhiPipelineResourceLayoutDesc& layout)
    {
        rhi::RhiShaderModuleDesc shaderDesc = {};
        shaderDesc.stage = rhi::RhiShaderStage::Compute;
        shaderDesc.codeFormat = rhi::RhiShaderCodeFormat::Source;
        shaderDesc.source = source;
        shaderDesc.entryPoint = "CSMain";
        shaderDesc.debugName = name;
        rhi::RhiShaderModule* shader = context.frameData.shaderManager->GetOrCompileShader(context.device, std::move(shaderId), shaderDesc);
        if (shader == nullptr)
        {
            return nullptr;
        }
        rhi::RhiComputePipelineDesc desc = {};
        desc.computeShader = shader;
        desc.resourceLayout = layout;
        desc.debugName = name;
        return context.frameData.shaderManager->GetOrCreateComputePipeline(context.device, ComputePipelineID{name, 0}, desc);
    }
} // namespace ve
