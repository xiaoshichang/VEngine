#include "Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderUniformBuffer.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/RHIPipelineManager.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <cstdint>
#include <exception>
#include <iterator>
#include <string>

namespace ve
{
    namespace
    {
        inline constexpr const char* DepthPrePassName = "DepthPrePass";

        struct DepthPrePassData
        {
            UInt32 viewIndex = 0;
            FrameGraphTextureHandle depth;
        };

        [[noreturn]] void FailDepthPrePass(const std::string& message)
        {
            VE_LOG_ERROR("{}: {}", DepthPrePassName, message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message.c_str());
            std::terminate();
        }

        [[noreturn]] void FailDepthPrePassItem(SizeT itemIndex, const char* message)
        {
            FailDepthPrePass("render item[" + std::to_string(itemIndex) + "] " + message);
        }

        [[nodiscard]] Int32 BuildPipelineVariant(rhi::RhiFillMode fillMode) noexcept
        {
            return static_cast<Int32>(fillMode);
        }

        [[nodiscard]] std::string BuildPipelineName(const RTShaderResource& shaderResource, const rhi::RhiShaderModule& vertexShader)
        {
            std::string name = DepthPrePassName;
            name += ":";
            name += shaderResource.GetDesc().name;
            name += ":Revision=";
            name += std::to_string(shaderResource.GetRevision());
            name += ":VS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(&vertexShader));
            return name;
        }

        [[nodiscard]] rhi::RhiGraphicsPipelineState* GetDepthPipeline(RenderPassContext& context,
                                                                      RTShaderResource& shaderResource,
                                                                      rhi::RhiFillMode fillMode)
        {
            RTShaderPass* shaderPass = shaderResource.GetPass(ShaderPassType::DepthOnly);
            rhi::RhiShaderModule* vertexShader = shaderPass != nullptr ? shaderPass->GetVertexShader() : nullptr;
            if (vertexShader == nullptr || context.frameData.pipelineManager == nullptr)
            {
                return nullptr;
            }

            const rhi::RhiVertexAttributeDesc attributes[] = { {"POSITION", 0, rhi::RhiFormat::Rgb32Float, 0},
                                                                {"NORMAL", 0, rhi::RhiFormat::Rgb32Float, sizeof(Float32) * 3} };
            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 1},
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 2},
            };
            rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
            pipelineDesc.blendState = rhi::StaticRenderStates::OpaqueBlend;
            pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
            pipelineDesc.rasterizerState.fillMode = fillMode;
            pipelineDesc.depthStencilState = rhi::StaticRenderStates::DepthReadWriteLessEqual;
            pipelineDesc.boundShaderState.vertexShader = vertexShader;
            pipelineDesc.boundShaderState.vertexDeclaration = {attributes, static_cast<UInt32>(std::size(attributes)), sizeof(RTMeshVertex)};
            pipelineDesc.resourceLayout = {bindings, static_cast<UInt32>(std::size(bindings))};
            pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
            pipelineDesc.colorAttachmentCount = 0;
            pipelineDesc.colorFormat = rhi::RhiFormat::Unknown;
            pipelineDesc.depthFormat = rhi::RhiFormat::Depth32Float;
            pipelineDesc.debugName = DepthPrePassName;
            return context.frameData.pipelineManager->GetOrCreateGraphicsPipeline(
                context.device,
                GraphicsPipelineID{BuildPipelineName(shaderResource, *vertexShader), BuildPipelineVariant(fillMode)},
                pipelineDesc);
        }
    } // namespace

    void DepthPrePass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex)
    {
        if (viewIndex >= graphData.views.size() || viewIndex >= frameGraph.GetRendererData().views.size() || !graphData.views[viewIndex].depth.IsValid())
        {
            FailDepthPrePass("registration requires a valid indexed view and depth target.");
        }

        frameGraph.AddRasterPass<DepthPrePassData>(
            std::string(DepthPrePassName) + "[" + std::to_string(viewIndex) + "]",
            [&graphData, viewIndex](FrameGraphBuilder& builder, DepthPrePassData& passData)
            {
                RendererViewFrameGraphData& viewGraphData = graphData.views[viewIndex];
                passData.viewIndex = viewIndex;
                passData.depth = builder.WriteDepthAttachment(viewGraphData.depth, rhi::RhiLoadAction::Clear);
                viewGraphData.depth = passData.depth;
            },
            [this](const DepthPrePassData& passData, RenderPassContext& context)
            {
                Draw(passData.viewIndex, context);
            });
    }

    void DepthPrePass::Draw(UInt32 viewIndex, RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();
        if (!context.executionInfo.depthEnabled || context.executionInfo.depthReadOnly || viewIndex >= context.rendererData.views.size())
        {
            FailDepthPrePass("execution requires a writable depth attachment and a valid renderer view index.");
        }

        const RendererViewData& viewData = context.GetView(viewIndex);
        const std::vector<std::shared_ptr<RTRenderItem>>& items = context.rendererData.opaqueItems;
        if (viewData.view.camera == nullptr || items.empty())
        {
            return;
        }

        const rhi::RhiRenderArea& renderArea = context.executionInfo.renderArea;
        const UniformBufferAllocation viewUniform = context.frameData.GetViewUniform(
            *viewData.view.viewState, viewData.view.camera.get(), rhi::RhiExtent2D{renderArea.width, renderArea.height});
        if (viewUniform.buffer == nullptr)
        {
            FailDepthPrePass("failed to allocate the required view uniform.");
        }

        rhi::RhiCommandList& commandList = context.commandList;
        for (SizeT itemIndex = 0; itemIndex < items.size(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem>& item = items[itemIndex];
            if (item == nullptr)
            {
                FailDepthPrePassItem(itemIndex, "is null.");
            }

            const auto mesh = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
            const auto material = std::dynamic_pointer_cast<RTMaterialResource>(item->GetMaterialResource());
            if (mesh == nullptr)
            {
                FailDepthPrePassItem(itemIndex, "requires an RTMeshResource.");
            }
            if (mesh->GetVertexBuffer() == nullptr || mesh->GetVertexStride() == 0 || mesh->GetVertexCount() == 0)
            {
                FailDepthPrePassItem(itemIndex, "requires a non-empty vertex buffer, stride, and vertex count.");
            }
            if ((mesh->GetIndexBuffer() == nullptr) != (mesh->GetIndexCount() == 0))
            {
                FailDepthPrePassItem(itemIndex, "requires its index buffer and index count to be both present or both absent.");
            }
            if (material == nullptr)
            {
                FailDepthPrePassItem(itemIndex, "requires an RTMaterialResource.");
            }
            if (material->GetShaderResource() == nullptr)
            {
                FailDepthPrePassItem(itemIndex, "requires an initialized shader resource.");
            }

            if (!material->GetShaderResource()->HasPass(ShaderPassType::DepthOnly))
            {
                continue;
            }
            rhi::RhiGraphicsPipelineState* pipeline = GetDepthPipeline(context, *material->GetShaderResource(), viewData.view.fillMode);
            if (pipeline == nullptr)
            {
                FailDepthPrePassItem(itemIndex, "failed to resolve its depth pipeline.");
            }

            const UniformBufferAllocation objectUniform = context.frameData.GetObjectUniform(*item);
            if (objectUniform.buffer == nullptr)
            {
                FailDepthPrePassItem(itemIndex, "failed to allocate its object uniform.");
            }

            commandList.SetPipeline(*pipeline);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 2, *objectUniform.buffer, objectUniform.offset, objectUniform.size);
            commandList.SetVertexBuffer(0, *mesh->GetVertexBuffer(), mesh->GetVertexStride(), 0);
            if (mesh->GetIndexBuffer() != nullptr && mesh->GetIndexCount() > 0)
            {
                commandList.SetIndexBuffer(*mesh->GetIndexBuffer(), rhi::RhiIndexFormat::UInt32, 0);
                commandList.DrawIndexed(mesh->GetIndexCount(), 0, 0);
            }
            else
            {
                commandList.Draw(mesh->GetVertexCount(), 0);
            }
        }
    }
} // namespace ve
