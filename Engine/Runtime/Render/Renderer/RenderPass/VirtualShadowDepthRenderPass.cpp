#include "Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderShaderIDs.h"
#include "Engine/Runtime/Render/RenderViewState.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <vector>

namespace ve
{
    namespace
    {
        inline constexpr const char* VirtualShadowDepthPassName = "VirtualShadowDepthPass";

        inline constexpr const char* VirtualShadowClearHlsl = R"(
struct VSOutput { float4 position : SV_POSITION; };
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    const float2 positions[3] = {float2(-1.0f, -1.0f), float2(-1.0f, 3.0f), float2(3.0f, -1.0f)};
    VSOutput output;
    output.position = float4(positions[vertexID], 1.0f, 1.0f);
    return output;
}
)";

        inline constexpr const char* VirtualShadowClearMsl = R"(
#include <metal_stdlib>
using namespace metal;
struct VSOutput { float4 position [[position]]; };
vertex VSOutput VSMain(uint vertexID [[vertex_id]])
{
    const float2 positions[3] = {float2(-1.0f, -1.0f), float2(-1.0f, 3.0f), float2(3.0f, -1.0f)};
    VSOutput output;
    output.position = float4(positions[vertexID], 1.0f, 1.0f);
    return output;
}
)";

        struct VirtualShadowDepthPassData
        {
            FrameGraphTextureHandle atlas;
        };

        [[nodiscard]] std::string BuildCasterPipelineName(const RTShaderResource& shaderResource, Float32 depthBias, Float32 normalBias)
        {
            std::string name = VirtualShadowDepthPassName;
            name += ":";
            name += shaderResource.GetDesc().name;
            name += ":VS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(shaderResource.GetVertexShader()));
            name += ":DB=";
            name += std::to_string(depthBias);
            name += ":NB=";
            name += std::to_string(normalBias);
            return name;
        }
    } // namespace

    void VirtualShadowDepthRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        const std::shared_ptr<VirtualShadowFramePacket>& packet = frameGraph.GetRendererData().virtualShadowPacket;
        if (!graphData.virtualShadowAtlas.IsValid() || packet == nullptr || !packet->enabled || packet->dirtyPages.empty())
        {
            return;
        }

        frameGraph.AddRasterPass<VirtualShadowDepthPassData>(
            VirtualShadowDepthPassName,
            [&graphData, atlasExtent = packet->atlasExtent](FrameGraphBuilder& builder, VirtualShadowDepthPassData& passData)
            {
                passData.atlas = builder.WriteDepthAttachment(graphData.virtualShadowAtlas, rhi::RhiLoadAction::Load);
                graphData.virtualShadowAtlas = passData.atlas;
                builder.SetRenderArea(rhi::RhiRenderArea{0, 0, atlasExtent, atlasExtent});
                builder.SetViewport(rhi::RhiViewport{0.0f, 0.0f, static_cast<Float32>(atlasExtent), static_cast<Float32>(atlasExtent), 0.0f, 1.0f});
                builder.SetScissor(rhi::RhiScissorRect{0, 0, atlasExtent, atlasExtent});
            },
            [this](const VirtualShadowDepthPassData&, RenderPassContext& context) { return Draw(context); });
    }

    ErrorCode VirtualShadowDepthRenderPass::Draw(RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();
        const std::shared_ptr<VirtualShadowFramePacket>& packet = context.rendererData.virtualShadowPacket;
        if (packet == nullptr || context.rendererData.viewState == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        rhi::RhiPipelineState* clearPipeline = GetClearPipeline(context);
        if (clearPipeline == nullptr)
        {
            return ErrorCode::PlatformError;
        }

        rhi::RhiCommandList& commandList = context.commandList;
        std::vector<VirtualShadowPageKey> renderedKeys;
        renderedKeys.reserve(packet->dirtyPages.size());
        for (const VirtualShadowDirtyPageDraw& page : packet->dirtyPages)
        {
            const Int32 slotX = static_cast<Int32>(page.physicalOrigin.x) - static_cast<Int32>(VirtualShadowPageGutter);
            const Int32 slotY = static_cast<Int32>(page.physicalOrigin.y) - static_cast<Int32>(VirtualShadowPageGutter);
            commandList.SetViewport(rhi::RhiViewport{static_cast<Float32>(slotX),
                                                     static_cast<Float32>(slotY),
                                                     static_cast<Float32>(VirtualShadowPhysicalPageSize),
                                                     static_cast<Float32>(VirtualShadowPhysicalPageSize),
                                                     0.0f,
                                                     1.0f});
            commandList.SetScissor(rhi::RhiScissorRect{slotX, slotY, VirtualShadowPhysicalPageSize, VirtualShadowPhysicalPageSize});
            commandList.SetPipeline(*clearPipeline);
            commandList.Draw(3, 0);

            ViewUniformData pageView = {};
            pageView.viewProjection = page.pageViewProjection.Transposed();
            const UniformBufferAllocation pageUniform = context.frameData.UploadUniform(&pageView, sizeof(pageView));
            for (const RTRenderItem* caster : page.casters)
            {
                if (caster == nullptr)
                {
                    continue;
                }
                const auto meshResource = std::dynamic_pointer_cast<RTMeshResource>(caster->GetMeshResource());
                const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(caster->GetMaterialResource());
                if (meshResource == nullptr || meshResource->GetVertexBuffer() == nullptr || materialResource == nullptr ||
                    materialResource->GetShaderResource() == nullptr)
                {
                    continue;
                }

                rhi::RhiPipelineState* casterPipeline = GetCasterPipeline(context, *materialResource->GetShaderResource(), *packet);
                if (casterPipeline == nullptr)
                {
                    return ErrorCode::PlatformError;
                }
                commandList.SetPipeline(*casterPipeline);
                commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *pageUniform.buffer, pageUniform.offset, pageUniform.size);
                const UniformBufferAllocation objectUniform = context.frameData.GetObjectUniform(*caster);
                commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 2, *objectUniform.buffer, objectUniform.offset, objectUniform.size);
                commandList.SetVertexBuffer(0, *meshResource->GetVertexBuffer(), meshResource->GetVertexStride(), 0);
                if (meshResource->GetIndexBuffer() != nullptr && meshResource->GetIndexCount() > 0)
                {
                    commandList.SetIndexBuffer(*meshResource->GetIndexBuffer(), rhi::RhiIndexFormat::UInt32, 0);
                    commandList.DrawIndexed(meshResource->GetIndexCount(), 0, 0);
                }
                else
                {
                    commandList.Draw(meshResource->GetVertexCount(), 0);
                }
            }
            renderedKeys.push_back(page.key);
        }

        VirtualShadowViewCache& cache = context.rendererData.viewState->GetVirtualShadowViewCache();
        cache.MarkRendered(renderedKeys);
        packet->residentPageTable = cache.GetPageCache().BuildResidentPageTable();
        packet->statistics.rendered = static_cast<UInt32>(renderedKeys.size());
        packet->statistics.resident = cache.GetPageCache().GetResidentPageCount();
        packet->statistics.dirty = cache.GetPageCache().GetDirtyPageCount();
        return ErrorCode::None;
    }

    rhi::RhiPipelineState* VirtualShadowDepthRenderPass::GetClearPipeline(RenderPassContext& context)
    {
        ShaderManager* shaderManager = context.frameData.shaderManager;
        if (shaderManager == nullptr)
        {
            return nullptr;
        }

        rhi::RhiShaderModuleDesc shaderDesc = {};
        shaderDesc.stage = rhi::RhiShaderStage::Vertex;
        shaderDesc.codeFormat = rhi::RhiShaderCodeFormat::Source;
        shaderDesc.source = context.device.GetBackend() == rhi::RhiBackend::Metal ? VirtualShadowClearMsl : VirtualShadowClearHlsl;
        shaderDesc.entryPoint = "VSMain";
        shaderDesc.debugName = VirtualShadowDepthPassName;
        rhi::RhiShaderModule* vertexShader =
            shaderManager->GetOrCompileShader(context.device, RenderShaderIDs::VirtualShadowClearVertex, shaderDesc);
        if (vertexShader == nullptr)
        {
            return nullptr;
        }

        rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.blendState = rhi::StaticRenderStates::OpaqueBlend;
        pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidNoCullRasterizer;
        pipelineDesc.rasterizerState.scissorEnabled = true;
        pipelineDesc.depthStencilState = rhi::StaticRenderStates::DepthReadWriteAlways;
        pipelineDesc.boundShaderState.vertexShader = vertexShader;
        pipelineDesc.colorAttachmentCount = 0;
        pipelineDesc.colorFormat = rhi::RhiFormat::Unknown;
        pipelineDesc.depthFormat = rhi::RhiFormat::Depth32Float;
        pipelineDesc.debugName = "VirtualShadowClearPipeline";
        return shaderManager->GetOrCreateGraphicsPipeline(
            context.device, GraphicsPipelineID{"VirtualShadowClearPipeline", 0}, pipelineDesc);
    }

    rhi::RhiPipelineState*
    VirtualShadowDepthRenderPass::GetCasterPipeline(RenderPassContext& context, RTShaderResource& shaderResource, const VirtualShadowFramePacket& packet)
    {
        ShaderManager* shaderManager = context.frameData.shaderManager;
        rhi::RhiShaderModule* vertexShader = shaderResource.GetVertexShader();
        if (shaderManager == nullptr || vertexShader == nullptr)
        {
            return nullptr;
        }

        rhi::RhiVertexAttributeDesc positionAttribute = {};
        positionAttribute.semanticName = "POSITION";
        positionAttribute.format = rhi::RhiFormat::Rgb32Float;
        rhi::RhiVertexAttributeDesc normalAttribute = {};
        normalAttribute.semanticName = "NORMAL";
        normalAttribute.format = rhi::RhiFormat::Rgb32Float;
        normalAttribute.offset = sizeof(Float32) * 3;
        const rhi::RhiVertexAttributeDesc vertexAttributes[] = {positionAttribute, normalAttribute};
        const rhi::RhiPipelineResourceBindingDesc bindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 1},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 2},
        };

        rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.blendState = rhi::StaticRenderStates::OpaqueBlend;
        pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
        pipelineDesc.rasterizerState.scissorEnabled = true;
        pipelineDesc.rasterizerState.depthBias = static_cast<Int32>(std::lround(packet.depthBias * 8388608.0f));
        pipelineDesc.rasterizerState.slopeScaledDepthBias = packet.normalBias;
        pipelineDesc.depthStencilState = rhi::StaticRenderStates::DepthReadWriteLessEqual;
        pipelineDesc.boundShaderState.vertexShader = vertexShader;
        pipelineDesc.boundShaderState.vertexDeclaration.attributes = vertexAttributes;
        pipelineDesc.boundShaderState.vertexDeclaration.attributeCount = static_cast<UInt32>(std::size(vertexAttributes));
        pipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(RTMeshVertex);
        pipelineDesc.resourceLayout.bindings = bindings;
        pipelineDesc.resourceLayout.bindingCount = static_cast<UInt32>(std::size(bindings));
        pipelineDesc.colorAttachmentCount = 0;
        pipelineDesc.colorFormat = rhi::RhiFormat::Unknown;
        pipelineDesc.depthFormat = rhi::RhiFormat::Depth32Float;
        pipelineDesc.debugName = VirtualShadowDepthPassName;
        const std::string pipelineName = BuildCasterPipelineName(shaderResource, packet.depthBias, packet.normalBias);
        return shaderManager->GetOrCreateGraphicsPipeline(context.device, GraphicsPipelineID{pipelineName, 0}, pipelineDesc);
    }
} // namespace ve
