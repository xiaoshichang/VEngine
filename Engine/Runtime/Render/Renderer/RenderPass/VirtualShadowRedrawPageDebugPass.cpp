#include "Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowRedrawPageDebugPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <exception>
#include <iterator>
#include <string>

namespace ve
{
    namespace
    {
        inline constexpr const char* DebugPassName = "VirtualShadowRedrawPageDebugPass";
        inline constexpr UInt32 DebugPageMaxAge = 60;

        struct DebugPassData
        {
            UInt32 viewIndex = 0;
            FrameGraphTextureHandle color;
            FrameGraphTextureHandle depth;
            FrameGraphTextureHandle virtualShadowAtlas;
            FrameGraphBufferHandle virtualShadowPageTable;
            FrameGraphBufferHandle physicalPages;
            VirtualShadowSamplingSnapshot virtualShadowSampling;
        };

        inline const char* DebugShaderSource = R"(
cbuffer FrameConstants : register(b0)
{
    float4 directionalLightDirection;
    float4 directionalLightColorAndIntensity;
    float4 ambientColor;
};

cbuffer ViewConstants : register(b1)
{
    float4x4 viewProjection;
    float4 cameraWorldPosition;
    float4 cameraWorldForward;
};

cbuffer ObjectConstants : register(b2)
{
    float4x4 localToWorld;
    uint receiveShadows;
    uint3 objectPadding;
};

struct VirtualShadowClipmapConstants
{
    float4 lightOriginAndPageWorldSize;
    float4 radiusAndDepthRange;
    int4 pageData;
};

cbuffer VirtualShadowConstants : register(b4)
{
    float4 virtualShadowLightRight;
    float4 virtualShadowLightUp;
    float4 virtualShadowLightDirection;
    float4 virtualShadowAtlasAndBias;
    VirtualShadowClipmapConstants virtualShadowClipmaps[24];
    uint virtualShadowAtlasExtent;
    uint virtualShadowPhysicalPageSize;
    uint virtualShadowClipmapLevelCount;
    uint virtualShadowAtlasPadding;
    float4x4 virtualShadowInverseViewProjection;
    uint virtualShadowScreenWidth;
    uint virtualShadowScreenHeight;
    uint virtualShadowPhysicalPageCapacity;
    uint virtualShadowFrameIndex;
};

Texture2D<uint> VirtualShadowAtlas : register(t1);
StructuredBuffer<uint> VirtualShadowDensePageTable : register(t5);

struct PhysicalPage
{
    uint key0;
    uint key1;
    uint lastUsedFrame;
    uint lastRenderedFrame;
    uint flags;
    uint3 padding;
};

StructuredBuffer<PhysicalPage> PhysicalPages : register(t6);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL0;
    float3 worldPosition : TEXCOORD0;
    nointerpolation uint receiveShadows : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPosition = mul(localToWorld, float4(input.position, 1.0f));
    output.position = mul(viewProjection, worldPosition);
    output.worldNormal = mul((float3x3)localToWorld, input.normal);
    output.worldPosition = worldPosition.xyz;
    output.receiveShadows = receiveShadows;
    return output;
}

uint FindVirtualShadowPhysicalPage(uint level, int2 pageCoordinate)
{
    int2 localPage = pageCoordinate - (virtualShadowClipmaps[level].pageData.xy - int2(64, 64));
    if (any(localPage < 0) || any(localPage >= 128))
    {
        return 0xFFFFFFFFu;
    }

    uint logicalIndex = level * 16384u + uint(localPage.y) * 128u + uint(localPage.x);
    uint denseEntry = VirtualShadowDensePageTable[logicalIndex];
    return denseEntry == 0u ? 0xFFFFFFFFu : denseEntry - 1u;
}

float SampleVirtualShadowPage(uint physicalPageIndex, float2 pagePosition, float receiverDepth)
{
    uint pagesPerRow = virtualShadowAtlasExtent / virtualShadowPhysicalPageSize;
    uint2 physicalPage = uint2(physicalPageIndex % pagesPerRow, physicalPageIndex / pagesPerRow);
    uint2 pageOrigin = physicalPage * virtualShadowPhysicalPageSize;
    uint2 pagePixel = min(uint2(saturate(pagePosition) * virtualShadowPhysicalPageSize),
                          uint2(virtualShadowPhysicalPageSize - 1u, virtualShadowPhysicalPageSize - 1u));
    uint encodedDepth = VirtualShadowAtlas.Load(int3(pageOrigin + pagePixel, 0));
    if (encodedDepth == 0u)
    {
        return 1.0f;
    }

    float casterDepth = 1.0f - asfloat(encodedDepth);
    return receiverDepth <= casterDepth ? 1.0f : 0.0f;
}

bool TryResolveVirtualShadowPage(float3 worldPosition,
                                 float3 worldNormal,
                                 out uint physicalPageIndex,
                                 out float2 pagePosition,
                                 out float depthReference)
{
    float cameraDepth = max(dot(worldPosition - cameraWorldPosition.xyz, cameraWorldForward.xyz), 0.0f);
    uint firstLevel = virtualShadowClipmapLevelCount - 1u;
    [loop]
    for (uint levelIndex = 0u; levelIndex < virtualShadowClipmapLevelCount; ++levelIndex)
    {
        if (cameraDepth <= virtualShadowClipmaps[levelIndex].radiusAndDepthRange.x)
        {
            firstLevel = levelIndex;
            break;
        }
    }

    float3 biasedWorldPosition = worldPosition + normalize(worldNormal) * virtualShadowAtlasAndBias.z;
    float3 lightPosition = float3(dot(biasedWorldPosition, virtualShadowLightRight.xyz),
                                  dot(biasedWorldPosition, virtualShadowLightUp.xyz),
                                  dot(biasedWorldPosition, virtualShadowLightDirection.xyz));
    [loop]
    for (uint sampleLevel = firstLevel; sampleLevel < virtualShadowClipmapLevelCount; ++sampleLevel)
    {
        VirtualShadowClipmapConstants clipmap = virtualShadowClipmaps[sampleLevel];
        float pageWorldSize = clipmap.lightOriginAndPageWorldSize.w;
        int2 pageCoordinate = int2(floor(lightPosition.xy / pageWorldSize));
        uint pageIndex = FindVirtualShadowPhysicalPage(sampleLevel, pageCoordinate);
        if (pageIndex == 0xFFFFFFFFu || pageIndex >= virtualShadowPhysicalPageCapacity)
        {
            continue;
        }

        float depthRange = clipmap.radiusAndDepthRange.z - clipmap.radiusAndDepthRange.y;
        float resolvedDepthReference = (lightPosition.z - clipmap.radiusAndDepthRange.y) / depthRange - virtualShadowAtlasAndBias.y;
        if (resolvedDepthReference < 0.0f || resolvedDepthReference > 1.0f)
        {
            continue;
        }

        physicalPageIndex = pageIndex;
        pagePosition = lightPosition.xy / pageWorldSize - float2(pageCoordinate);
        depthReference = resolvedDepthReference;
        return true;
    }

    physicalPageIndex = 0xFFFFFFFFu;
    pagePosition = float2(0.0f, 0.0f);
    depthReference = 0.0f;
    return false;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 normal = normalize(input.worldNormal);
    uint physicalPageIndex;
    float2 pagePosition;
    float depthReference;
    bool pageResolved = TryResolveVirtualShadowPage(input.worldPosition, normal, physicalPageIndex, pagePosition, depthReference);

    float shadowVisibility = 1.0f;
    float3 redrawTint = float3(0.45f, 0.45f, 0.45f);
    if (pageResolved)
    {
        PhysicalPage page = PhysicalPages[physicalPageIndex];
        if (input.receiveShadows != 0u)
        {
            shadowVisibility = SampleVirtualShadowPage(physicalPageIndex, pagePosition, depthReference);
        }

        uint age = virtualShadowFrameIndex - page.lastRenderedFrame;
        float ageFactor = saturate(float(age) / float()";

        inline const std::string CompleteDebugShaderSource =
            std::string(DebugShaderSource) + std::to_string(DebugPageMaxAge) + R"());
        redrawTint = lerp(float3(1.0f, 0.04f, 0.02f), float3(0.04f, 0.9f, 0.12f), ageFactor);
    }

    float3 lightDirection = normalize(directionalLightDirection.xyz);
    float diffuse = saturate(dot(normal, -lightDirection));
    float3 directLight = directionalLightColorAndIntensity.rgb * directionalLightColorAndIntensity.w * diffuse * shadowVisibility;
    float3 lighting = ambientColor.rgb + directLight;
    float3 debugBaseColor = lerp(float3(0.25f, 0.25f, 0.25f), redrawTint, 0.8f);
    return float4(saturate(debugBaseColor * lighting), 1.0f);
}
)";

        [[noreturn]] void FailDebugPass(const std::string& message)
        {
            VE_LOG_ERROR("{}: {}", DebugPassName, message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message.c_str());
            std::terminate();
        }

        [[noreturn]] void FailDebugItem(const char* queueName, SizeT itemIndex, const char* message)
        {
            FailDebugPass(std::string(queueName) + " render item[" + std::to_string(itemIndex) + "] " + message);
        }

        void ValidateVirtualShadowBindings(const ResolvedFrameGraphTexture& atlas,
                                           const ResolvedFrameGraphBuffer& table,
                                           const ResolvedFrameGraphBuffer& physicalPages,
                                           const VirtualShadowSamplingSnapshot& sampling)
        {
            if (atlas.texture == nullptr || table.buffer == nullptr || physicalPages.buffer == nullptr || sampling.atlas == nullptr ||
                sampling.pageTable == nullptr)
            {
                FailDebugPass("execution requires complete VSM atlas, page-table, and physical-page bindings.");
            }
            if (atlas.texture != sampling.atlas || table.buffer != sampling.pageTable)
            {
                FailDebugPass("resolved VSM resources do not match the view sampling snapshot.");
            }
            if (sampling.pageTableSize != VirtualShadowLogicalPageBufferSize || sampling.pageTableOffset > sampling.pageTable->GetSize() ||
                sampling.pageTableSize > sampling.pageTable->GetSize() - sampling.pageTableOffset)
            {
                FailDebugPass("VSM page-table binding does not cover one valid logical page-table slice.");
            }

            const UInt64 requiredPhysicalPageSize =
                static_cast<UInt64>(sampling.constants.physicalPageCapacity) * sizeof(VirtualShadowGpuPhysicalPage);
            if (sampling.constants.physicalPageCapacity == 0 || requiredPhysicalPageSize > physicalPages.buffer->GetSize())
            {
                FailDebugPass("VSM physical-page binding does not cover the declared physical-page capacity.");
            }
        }

        [[nodiscard]] Int32 BuildPipelineVariant(rhi::RhiFormat targetFormat, rhi::RhiFillMode fillMode, bool depthEnabled) noexcept
        {
            return static_cast<Int32>(targetFormat) | (static_cast<Int32>(fillMode) << 8) | (depthEnabled ? (1 << 16) : 0);
        }
    } // namespace

    void VirtualShadowRedrawPageDebugPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex)
    {
        if (viewIndex >= graphData.views.size() || viewIndex >= frameGraph.GetRendererData().views.size())
        {
            FailDebugPass("registration requires a valid renderer view index.");
        }

        RendererViewFrameGraphData& viewGraphData = graphData.views[viewIndex];
        if (!viewGraphData.color.IsValid() || !viewGraphData.depth.IsValid() || !graphData.virtualShadowAtlas.IsValid() ||
            !viewGraphData.virtualShadowPageTable.IsValid() || !graphData.virtualShadowPhysicalPages.IsValid())
        {
            FailDebugPass("registration requires valid color, depth, and VSM graph resources.");
        }

        frameGraph.AddRasterPass<DebugPassData>(
            std::string(DebugPassName) + "[View=" + std::to_string(viewIndex) + "]",
            [viewIndex, &graphData](FrameGraphBuilder& builder, DebugPassData& passData)
            {
                RendererViewFrameGraphData& viewData = graphData.views[viewIndex];
                const RenderView& view = builder.GetRendererData().views[viewIndex].view;
                passData.viewIndex = viewIndex;
                const rhi::RhiColor clearColor = view.camera != nullptr ? view.camera->GetClearColor() : rhi::RhiColor{};
                passData.color = builder.WriteColorAttachment(viewData.color, view.target.colorLoadAction, clearColor);
                viewData.color = passData.color;
                passData.depth = builder.ReadDepthAttachment(viewData.depth);
                passData.virtualShadowAtlas = builder.Read(graphData.virtualShadowAtlas);
                passData.virtualShadowPageTable = builder.Read(viewData.virtualShadowPageTable);
                passData.physicalPages = builder.Read(graphData.virtualShadowPhysicalPages);
                passData.virtualShadowSampling = viewData.virtualShadowSampling;
            },
            [this](const DebugPassData& passData, const FrameGraphPassResources& resources, RenderPassContext& context)
            {
                Draw(resources,
                     passData.virtualShadowAtlas,
                     passData.virtualShadowPageTable,
                     passData.physicalPages,
                     passData.virtualShadowSampling,
                     passData.viewIndex,
                     context);
            });
    }

    void VirtualShadowRedrawPageDebugPass::Draw(const FrameGraphPassResources& resources,
                                                FrameGraphTextureHandle virtualShadowAtlas,
                                                FrameGraphBufferHandle virtualShadowPageTable,
                                                FrameGraphBufferHandle physicalPages,
                                                const VirtualShadowSamplingSnapshot& virtualShadowSampling,
                                                UInt32 viewIndex,
                                                RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();
        if (viewIndex >= context.rendererData.views.size() || !context.executionInfo.depthEnabled || !context.executionInfo.depthReadOnly)
        {
            FailDebugPass("execution requires a valid view and a read-only prepass depth attachment.");
        }

        const ResolvedFrameGraphTexture atlas = resources.GetTexture(virtualShadowAtlas);
        const ResolvedFrameGraphBuffer table = resources.GetBuffer(virtualShadowPageTable);
        const ResolvedFrameGraphBuffer pages = resources.GetBuffer(physicalPages);
        ValidateVirtualShadowBindings(atlas, table, pages, virtualShadowSampling);

        const RendererViewData& viewData = context.GetView(viewIndex);
        if (viewData.view.camera == nullptr)
        {
            return;
        }
        if (context.rendererData.scene == nullptr)
        {
            FailDebugPass("execution requires a render scene.");
        }

        EnsurePipeline(context, viewIndex);

        const UniformBufferAllocation frameUniform = context.frameData.GetFrameUniform(*context.rendererData.scene);
        const rhi::RhiRenderArea& renderArea = context.executionInfo.renderArea;
        const UniformBufferAllocation viewUniform =
            context.frameData.GetViewUniform(viewData.view.camera.get(), rhi::RhiExtent2D{renderArea.width, renderArea.height});
        const VirtualShadowGpuConstants virtualShadowConstants = virtualShadowSampling.constants;
        const UniformBufferAllocation virtualShadowUniform = context.frameData.UploadUniform(&virtualShadowConstants, sizeof(virtualShadowConstants));
        if (frameUniform.buffer == nullptr || viewUniform.buffer == nullptr ||
            !virtual_shadow_detail::IsValidVirtualShadowUniformAllocation(virtualShadowUniform))
        {
            FailDebugPass("execution failed to allocate required frame, view, or VSM uniforms.");
        }

        rhi::RhiCommandList& commandList = context.commandList;
        commandList.SetPipeline(*pipelineState_);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 0, *frameUniform.buffer, frameUniform.offset, frameUniform.size);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
        commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
        BindVirtualShadowSampling(commandList, virtualShadowSampling, virtualShadowUniform);
        commandList.SetStorageBuffer(rhi::RhiShaderStage::Fragment, 6, *pages.buffer, 0, pages.buffer->GetSize());

        const auto drawItems = [&](const std::vector<std::shared_ptr<RTRenderItem>>& items, const char* queueName)
        {
            for (SizeT itemIndex = 0; itemIndex < items.size(); ++itemIndex)
            {
                const std::shared_ptr<RTRenderItem>& item = items[itemIndex];
                if (item == nullptr)
                {
                    FailDebugItem(queueName, itemIndex, "is null.");
                }

                const auto mesh = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
                if (mesh == nullptr)
                {
                    FailDebugItem(queueName, itemIndex, "requires an RTMeshResource.");
                }
                if (mesh->GetVertexBuffer() == nullptr || mesh->GetVertexStride() == 0 || mesh->GetVertexCount() == 0)
                {
                    FailDebugItem(queueName, itemIndex, "requires a non-empty vertex buffer, stride, and vertex count.");
                }
                if ((mesh->GetIndexBuffer() == nullptr) != (mesh->GetIndexCount() == 0))
                {
                    FailDebugItem(queueName, itemIndex, "requires its index buffer and index count to be both present or both absent.");
                }

                const UniformBufferAllocation objectUniform = context.frameData.GetObjectUniform(*item);
                if (objectUniform.buffer == nullptr)
                {
                    FailDebugItem(queueName, itemIndex, "failed to allocate its object uniform.");
                }

                commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 2, *objectUniform.buffer, objectUniform.offset, objectUniform.size);
                commandList.SetVertexBuffer(0, *mesh->GetVertexBuffer(), mesh->GetVertexStride(), 0);
                if (mesh->GetIndexBuffer() != nullptr)
                {
                    commandList.SetIndexBuffer(*mesh->GetIndexBuffer(), rhi::RhiIndexFormat::UInt32, 0);
                    commandList.DrawIndexed(mesh->GetIndexCount(), 0, 0);
                }
                else
                {
                    commandList.Draw(mesh->GetVertexCount(), 0);
                }
            }
        };

        drawItems(context.rendererData.opaqueItems, "opaque");
        drawItems(viewData.transparentItems, "transparent");
    }

    void VirtualShadowRedrawPageDebugPass::EnsurePipeline(RenderPassContext& context, UInt32 viewIndex)
    {
        const rhi::RhiFillMode fillMode = context.GetView(viewIndex).view.fillMode;
        const rhi::RhiFormat targetFormat = context.executionInfo.colorFormat;
        const bool depthEnabled = context.executionInfo.depthEnabled;
        if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat && pipelineFillMode_ == fillMode && pipelineDepthEnabled_ == depthEnabled)
        {
            return;
        }

        ShaderManager* shaderManager = context.frameData.shaderManager;
        if (shaderManager == nullptr)
        {
            FailDebugPass("pipeline creation requires the frame ShaderManager.");
        }

        const rhi::RhiShaderModuleDesc vertexShaderDesc = {rhi::RhiShaderStage::Vertex,
                                                           rhi::RhiShaderCodeFormat::Source,
                                                           CompleteDebugShaderSource.c_str(),
                                                           nullptr,
                                                           0,
                                                           "VSMain",
                                                           "VirtualShadowRedrawPageDebugVS"};
        rhi::RhiShaderModule* vertexShader =
            shaderManager->GetOrCompileShader(context.device, ShaderID{"VirtualShadow.RedrawPageDebug.Vertex", 1}, vertexShaderDesc);
        if (vertexShader == nullptr)
        {
            FailDebugPass("failed to compile the vertex shader.");
        }

        const rhi::RhiShaderModuleDesc fragmentShaderDesc = {rhi::RhiShaderStage::Fragment,
                                                             rhi::RhiShaderCodeFormat::Source,
                                                             CompleteDebugShaderSource.c_str(),
                                                             nullptr,
                                                             0,
                                                             "PSMain",
                                                             "VirtualShadowRedrawPageDebugPS"};
        rhi::RhiShaderModule* fragmentShader =
            shaderManager->GetOrCompileShader(context.device, ShaderID{"VirtualShadow.RedrawPageDebug.Fragment", 1}, fragmentShaderDesc);
        if (fragmentShader == nullptr)
        {
            FailDebugPass("failed to compile the fragment shader.");
        }

        const rhi::RhiVertexAttributeDesc attributes[] = {
            {"POSITION", 0, rhi::RhiFormat::Rgb32Float, 0},
            {"NORMAL", 0, rhi::RhiFormat::Rgb32Float, sizeof(Float32) * 3},
        };
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 0},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 1},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 1},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 2},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 4},
            {rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Fragment, 1},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Fragment, 5},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Fragment, 6},
        };
        rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.blendState = rhi::StaticRenderStates::OpaqueBlend;
        pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
        pipelineDesc.rasterizerState.fillMode = fillMode;
        pipelineDesc.depthStencilState = depthEnabled ? rhi::StaticRenderStates::DepthReadOnlyLessEqual : rhi::StaticRenderStates::DepthDisabled;
        pipelineDesc.boundShaderState.vertexShader = vertexShader;
        pipelineDesc.boundShaderState.fragmentShader = fragmentShader;
        pipelineDesc.boundShaderState.vertexDeclaration = {attributes, static_cast<UInt32>(std::size(attributes)), sizeof(RTMeshVertex)};
        pipelineDesc.resourceLayout = {bindings, static_cast<UInt32>(std::size(bindings))};
        pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorAttachmentCount = 1;
        pipelineDesc.colorFormat = targetFormat;
        pipelineDesc.depthFormat = depthEnabled ? rhi::RhiFormat::Depth32Float : rhi::RhiFormat::Unknown;
        pipelineDesc.debugName = DebugPassName;

        pipelineState_ = shaderManager->GetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{DebugPassName, BuildPipelineVariant(targetFormat, fillMode, depthEnabled)}, pipelineDesc);
        if (pipelineState_ == nullptr)
        {
            FailDebugPass("failed to create the debug graphics pipeline.");
        }

        pipelineColorFormat_ = targetFormat;
        pipelineFillMode_ = fillMode;
        pipelineDepthEnabled_ = depthEnabled;
    }
} // namespace ve
