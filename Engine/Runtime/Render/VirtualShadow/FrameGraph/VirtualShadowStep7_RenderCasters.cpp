#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"

#include <iterator>
#include <memory>
#include <string>

namespace ve
{
    namespace
    {
        inline const std::string Step7_RenderCastersHlsl = std::string(virtual_shadow_detail::VirtualShadowCommonHlsl) + R"(
cbuffer ObjectConstants : register(b2)
{
    float4x4 localToWorld;
    uint receiveShadows;
    uint3 objectPadding;
};

StructuredBuffer<uint> PageTable : register(t5);
StructuredBuffer<PhysicalPage> PhysicalPages : register(t2);
RWTexture2D<uint> PhysicalAtlas : register(u0);

struct Input
{
    float3 position : POSITION;
};

struct Output
{
    float4 position : SV_POSITION;
    nointerpolation uint level : TEXCOORD0;
    float normalizedDepth : TEXCOORD1;
};

Output VSMain(Input input, uint instanceID : SV_InstanceID)
{
    Output output;
    uint level = instanceID;
    float4 world = mul(localToWorld, float4(input.position, 1.0f));
    float3 light = float3(
        dot(world.xyz, lightRight.xyz),
        dot(world.xyz, lightUp.xyz),
        dot(world.xyz, lightForward.xyz));

    float pageWorldSize = clipmaps[level].originAndPageSize.w;
    float2 workingRegionMinimum =
        float2(clipmaps[level].pageData.xy - int2(64, 64)) * pageWorldSize;
    float2 virtualUv = (light.xy - workingRegionMinimum) /
                       (pageWorldSize * 128.0f);

    float depthRange = clipmaps[level].radiusAndDepth.z -
                       clipmaps[level].radiusAndDepth.y;
    output.position = float4(
        virtualUv.x * 2.0f - 1.0f,
        1.0f - virtualUv.y * 2.0f,
        0.0f,
        1.0f);
    output.level = level;
    output.normalizedDepth =
        (light.z - clipmaps[level].radiusAndDepth.y) / depthRange;
    return output;
}

void PSMain(Output input)
{
    if (input.level >= clipmapCount ||
        input.normalizedDepth < 0.0f ||
        input.normalizedDepth > 1.0f)
    {
        discard;
    }

    uint2 virtualPixel = uint2(input.position.xy);
    if (any(virtualPixel >= uint2(16384u, 16384u)))
    {
        discard;
    }

    uint2 localPage = virtualPixel / physicalPageSize;
    uint2 pagePixel = virtualPixel % physicalPageSize;
    int2 absolutePage =
        clipmaps[input.level].pageData.xy - int2(64, 64) +
        int2(localPage);
    uint expectedKey0 =
        (uint(absolutePage.x) & 0xFFFFu) |
        ((uint(absolutePage.y) & 0xFFFFu) << 16u);
    uint logicalIndex =
        input.level * 16384u + localPage.y * 128u + localPage.x;
    uint denseEntry = PageTable[logicalIndex];
    if (denseEntry == 0u)
    {
        discard;
    }

    uint physicalIndex = denseEntry - 1u;
    if (physicalIndex >= physicalCapacity)
    {
        discard;
    }

    PhysicalPage page = PhysicalPages[physicalIndex];
    bool matchesView =
        ((page.key1 >> 8u) & 0x00FFFFFFu) ==
        (viewID & 0x00FFFFFFu);
    bool matchesLevel = (page.key1 & 0xFFu) == input.level;
    if ((page.flags & 7u) != 7u ||
        page.key0 != expectedKey0 ||
        !matchesView ||
        !matchesLevel)
    {
        discard;
    }

    uint pagesPerRow = atlasExtent / physicalPageSize;
    uint2 slotOrigin =
        uint2(physicalIndex % pagesPerRow, physicalIndex / pagesPerRow) *
        physicalPageSize;
    float reversedDepth = max(
        1.0f - saturate(input.normalizedDepth),
        asfloat(1u));
    InterlockedMax(
        PhysicalAtlas[slotOrigin + pagePixel],
        asuint(reversedDepth));
}
)";

        [[nodiscard]] rhi::RhiPipelineState* GetStep7_RenderCastersPipeline(const FrameRenderPipelineData& frameData)
        {
            rhi::RhiShaderModuleDesc vertexShaderDesc = {rhi::RhiShaderStage::Vertex,
                                                         rhi::RhiShaderCodeFormat::Source,
                                                         Step7_RenderCastersHlsl.c_str(),
                                                         nullptr,
                                                         0,
                                                         "VSMain",
                                                         "VirtualShadowStep7_RenderCastersVS"};
            rhi::RhiShaderModule* vertexShader =
                frameData.shaderManager->GetOrCompileShader(*frameData.device, ShaderID{"VirtualShadow.Step7_RenderCasters.Vertex", 0}, vertexShaderDesc);
            if (vertexShader == nullptr)
            {
                return nullptr;
            }

            rhi::RhiShaderModuleDesc fragmentShaderDesc = {rhi::RhiShaderStage::Fragment,
                                                           rhi::RhiShaderCodeFormat::Source,
                                                           Step7_RenderCastersHlsl.c_str(),
                                                           nullptr,
                                                           0,
                                                           "PSMain",
                                                           "VirtualShadowStep7_RenderCastersPS"};
            rhi::RhiShaderModule* fragmentShader =
                frameData.shaderManager->GetOrCompileShader(*frameData.device, ShaderID{"VirtualShadow.Step7_RenderCasters.Fragment", 0}, fragmentShaderDesc);
            if (fragmentShader == nullptr)
            {
                return nullptr;
            }

            rhi::RhiVertexAttributeDesc attribute = {"POSITION", 0, rhi::RhiFormat::Rgb32Float, 0};
            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 2},
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 4},
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 4},
                {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Fragment, 2},
                {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Fragment, 5},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageTexture, rhi::RhiShaderStage::Fragment, 0},
            };
            rhi::RhiGraphicsPipelineDesc desc = {};
            desc.blendState = rhi::StaticRenderStates::OpaqueBlend;
            desc.rasterizerState = rhi::StaticRenderStates::SolidNoCullRasterizer;
            desc.depthStencilState = rhi::StaticRenderStates::DepthDisabled;
            desc.boundShaderState.vertexShader = vertexShader;
            desc.boundShaderState.fragmentShader = fragmentShader;
            desc.boundShaderState.vertexDeclaration = {&attribute, 1, sizeof(RTMeshVertex)};
            desc.resourceLayout = {bindings, static_cast<UInt32>(std::size(bindings))};
            desc.colorAttachmentCount = 0;
            desc.colorFormat = rhi::RhiFormat::Unknown;
            desc.depthFormat = rhi::RhiFormat::Unknown;
            return frameData.shaderManager->GetOrCreateGraphicsPipeline(*frameData.device, GraphicsPipelineID{"VirtualShadowStep7_RenderCasters", 0}, desc);
        }

        void RecordStep7_RenderCasters(const VirtualShadowPageRecordingContext& context,
                                       rhi::RhiBuffer& physicalPages,
                                       rhi::RhiBuffer& pageTable,
                                       rhi::RhiTexture& atlas)
        {
            if (!context.slice.IsValid() || context.slice.pageTableOffset % sizeof(UInt32) != 0 ||
                context.slice.pageTableOffset > pageTable.GetSize() ||
                VirtualShadowLogicalPageBufferSize > pageTable.GetSize() - context.slice.pageTableOffset)
            {
                FailVirtualShadow("VSM Step7_RenderCasters requires one exact, UInt32-aligned page-table slice.");
            }

            rhi::RhiPipelineState* pipeline = GetStep7_RenderCastersPipeline(context.frameData);
            if (pipeline == nullptr)
            {
                FailVirtualShadow("VSM Step7_RenderCasters failed to create its graphics pipeline.");
            }

            const UniformBufferAllocation constants =
                virtual_shadow_detail::UploadVirtualShadowPassConstants(context.frameData, virtual_shadow_detail::BuildVirtualShadowPageGpuConstants(context));
            rhi::RhiCommandList& commandList = context.frameData.GetCommandList();
            commandList.SetPipeline(*pipeline);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 4, *constants.buffer, constants.offset, constants.size);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 4, *constants.buffer, constants.offset, constants.size);
            commandList.SetStorageBuffer(rhi::RhiShaderStage::Fragment, 2, physicalPages, 0, physicalPages.GetSize());
            commandList.SetStorageBuffer(
                rhi::RhiShaderStage::Fragment, 5, pageTable, context.slice.pageTableOffset, VirtualShadowLogicalPageBufferSize);
            commandList.SetReadWriteStorageTexture(rhi::RhiShaderStage::Fragment, 0, atlas);
            for (const std::shared_ptr<RTRenderItem>& item : context.opaqueItems)
            {
                if (item == nullptr)
                {
                    FailVirtualShadow("VSM Step7_RenderCasters encountered a null opaque render item.");
                }
                if (!item->CastShadows())
                {
                    continue;
                }

                const std::shared_ptr<RTMeshResource> mesh = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
                if (mesh == nullptr)
                {
                    FailVirtualShadow("VSM Step7_RenderCasters requires every shadow caster to reference an RTMeshResource.");
                }
                if (mesh->GetVertexBuffer() == nullptr || mesh->GetVertexStride() == 0 || mesh->GetVertexCount() == 0)
                {
                    FailVirtualShadow("VSM Step7_RenderCasters requires every shadow caster to have a non-empty vertex buffer.");
                }
                const bool hasIndexBuffer = mesh->GetIndexBuffer() != nullptr;
                const bool hasIndices = mesh->GetIndexCount() != 0;
                if (hasIndexBuffer != hasIndices)
                {
                    FailVirtualShadow(
                        "VSM Step7_RenderCasters requires a shadow caster's index buffer and index count to be either both present or both absent.");
                }

                const UniformBufferAllocation objectUniform = context.frameData.GetObjectUniform(*item);
                if (objectUniform.buffer == nullptr)
                {
                    FailVirtualShadow("VSM Step7_RenderCasters failed to upload object constants.");
                }
                commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 2, *objectUniform.buffer, objectUniform.offset, objectUniform.size);
                commandList.SetVertexBuffer(0, *mesh->GetVertexBuffer(), mesh->GetVertexStride(), 0);
                if (hasIndexBuffer)
                {
                    commandList.SetIndexBuffer(*mesh->GetIndexBuffer(), rhi::RhiIndexFormat::UInt32, 0);
                    commandList.DrawIndexedInstanced(mesh->GetIndexCount(), VirtualShadowClipmapLevelCount, 0, 0, 0);
                    if (context.drawDiagnostics != nullptr)
                    {
                        ++context.drawDiagnostics->indexedDrawCount;
                    }
                }
                else
                {
                    commandList.DrawInstanced(mesh->GetVertexCount(), VirtualShadowClipmapLevelCount, 0, 0);
                }
            }
        }
    } // namespace

    void
    AddVirtualShadowStep7_RenderCastersPass(FrameGraph& frameGraph, const VirtualShadowPreparedViewGraphData& view, VirtualShadowFrameGraphResources& resources)
    {
        virtual_shadow_detail::ValidateVirtualShadowPageView(view, resources);
        frameGraph.AddRasterPass<virtual_shadow_detail::VirtualShadowViewPagePassData>(
            "VirtualShadow.Step7_RenderCasters[View=" + std::to_string(view.packet.viewID) + "]",
            [&resources, view](FrameGraphBuilder& builder, virtual_shadow_detail::VirtualShadowViewPagePassData& data)
            {
                data.viewIndex = view.viewIndex;
                data.packet = view.packet;
                data.slice = view.slice;
                data.physicalPageCapacity = resources.physicalPageCapacity;
                data.drawDiagnostics = view.drawDiagnostics;
                data.physicalPages = builder.Read(resources.physicalPages);
                data.pageTable = builder.Read(resources.pageTable);
                data.atlas = builder.Write(resources.atlas);
                resources.atlas = data.atlas;
                builder.AddUavBarrierBeforeExecute(data.atlas);
                builder.SetRenderArea({0, 0, VirtualShadowVirtualResolution, VirtualShadowVirtualResolution});
                builder.SetViewport({0.0f,
                                     0.0f,
                                     static_cast<Float32>(VirtualShadowVirtualResolution),
                                     static_cast<Float32>(VirtualShadowVirtualResolution),
                                     0.0f,
                                     1.0f});
                builder.SetScissor({0, 0, VirtualShadowVirtualResolution, VirtualShadowVirtualResolution});
            },
            [](const virtual_shadow_detail::VirtualShadowViewPagePassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            {
                static_cast<void>(context.GetView(data.viewIndex));
                RecordStep7_RenderCasters(virtual_shadow_detail::BuildVirtualShadowPagePassContext(data, context),
                                          virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.physicalPages),
                                          virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.pageTable),
                                          virtual_shadow_detail::RequireVirtualShadowTexture(passResources, data.atlas));
            });
    }
} // namespace ve
