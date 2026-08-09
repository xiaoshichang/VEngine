#include "Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RenderFrameUniformCache.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/RHIPipelineManager.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <cstdint>
#include <exception>
#include <iterator>
#include <string>

namespace ve
{
    namespace
    {
        inline constexpr const char* TransparentScenePassName = "TransparentScenePass";

        struct TransparentScenePassData
        {
            UInt32 viewIndex = 0;
            FrameGraphTextureHandle color;
            FrameGraphTextureHandle depth;
            FrameGraphTextureHandle virtualShadowAtlas;
            FrameGraphBufferHandle virtualShadowPageTable;
            VirtualShadowSamplingSnapshot virtualShadowSampling;
        };

        [[noreturn]] void FailTransparentScenePass(const std::string& message)
        {
            VE_LOG_ERROR("{}: {}", TransparentScenePassName, message);
            VE_ASSERT_ALWAYS_MESSAGE(false, message.c_str());
            std::terminate();
        }

        [[noreturn]] void FailTransparentSceneItem(SizeT itemIndex, const char* message)
        {
            FailTransparentScenePass("render item[" + std::to_string(itemIndex) + "] " + message);
        }

        void ValidateVirtualShadowBindings(const ResolvedFrameGraphTexture& atlas,
                                           const ResolvedFrameGraphBuffer& table,
                                           const VirtualShadowSamplingSnapshot& sampling)
        {
            if (atlas.texture == nullptr || table.buffer == nullptr || sampling.atlas == nullptr || sampling.pageTable == nullptr)
            {
                FailTransparentScenePass("execution requires complete VSM atlas and page-table bindings.");
            }
            if (atlas.texture != sampling.atlas || table.buffer != sampling.pageTable)
            {
                FailTransparentScenePass("resolved VSM resources do not match the view sampling snapshot.");
            }
            if (sampling.pageTableSize != VirtualShadowLogicalPageBufferSize || sampling.pageTableOffset > sampling.pageTable->GetSize() ||
                sampling.pageTableSize > sampling.pageTable->GetSize() - sampling.pageTableOffset)
            {
                FailTransparentScenePass("VSM page-table binding does not cover one valid logical page-table slice.");
            }
        }

        [[nodiscard]] Int32 BuildPipelineVariant(rhi::RhiFormat targetFormat, rhi::RhiFillMode fillMode, bool depthEnabled) noexcept
        {
            return static_cast<Int32>(targetFormat) | (static_cast<Int32>(fillMode) << 8) | (depthEnabled ? (1 << 16) : 0) | (1 << 17);
        }

        [[nodiscard]] std::string
        BuildPipelineName(const RTShaderResource& shaderResource, const rhi::RhiShaderModule& vertexShader, const rhi::RhiShaderModule& fragmentShader)
        {
            std::string name = TransparentScenePassName;
            name += ":";
            name += shaderResource.GetDesc().name;
            name += ":VS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(&vertexShader));
            name += ":FS=";
            name += std::to_string(reinterpret_cast<std::uintptr_t>(&fragmentShader));
            return name;
        }
    } // namespace

    TransparentSceneRenderPass::TransparentSceneRenderPass(TransparentSceneRenderPassInitParam initParam)
        : initParam_(initParam)
    {
    }

    void TransparentSceneRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex)
    {
        if (viewIndex >= graphData.views.size() || viewIndex >= frameGraph.GetRendererData().views.size())
        {
            FailTransparentScenePass("registration requires a valid renderer view index.");
        }
        frameGraph.AddRasterPass<TransparentScenePassData>(
            std::string(TransparentScenePassName) + "[" + std::to_string(viewIndex) + "]",
            [&graphData, viewIndex](FrameGraphBuilder& builder, TransparentScenePassData& passData)
            {
                RendererViewFrameGraphData& viewGraphData = graphData.views[viewIndex];
                passData.viewIndex = viewIndex;
                passData.color = builder.WriteColorAttachment(viewGraphData.color, rhi::RhiLoadAction::Load);
                viewGraphData.color = passData.color;

                if (viewGraphData.depth.IsValid())
                {
                    passData.depth = builder.ReadDepthAttachment(viewGraphData.depth);
                }

                if (!graphData.virtualShadowAtlas.IsValid() || !viewGraphData.virtualShadowPageTable.IsValid())
                {
                    FailTransparentScenePass("registration requires final VSM graph handles.");
                }
                passData.virtualShadowAtlas = builder.Read(graphData.virtualShadowAtlas);
                passData.virtualShadowPageTable = builder.Read(viewGraphData.virtualShadowPageTable);
                passData.virtualShadowSampling = viewGraphData.virtualShadowSampling;
            },
            [this](const TransparentScenePassData& passData, const FrameGraphPassResources& resources, RenderPassContext& context)
            {
                Draw(resources, passData.virtualShadowAtlas, passData.virtualShadowPageTable, passData.virtualShadowSampling, passData.viewIndex, context);
            });
    }

    void TransparentSceneRenderPass::Draw(const FrameGraphPassResources& resources,
                                          FrameGraphTextureHandle virtualShadowAtlas,
                                          FrameGraphBufferHandle virtualShadowPageTable,
                                          const VirtualShadowSamplingSnapshot& virtualShadowSampling,
                                          UInt32 viewIndex,
                                          RenderPassContext& context)
    {
        VE_ASSERT_RENDER_THREAD();
        if (!virtualShadowAtlas.IsValid() || !virtualShadowPageTable.IsValid())
        {
            FailTransparentScenePass("execution requires final VSM graph handles.");
        }
        const ResolvedFrameGraphTexture atlas = resources.GetTexture(virtualShadowAtlas);
        const ResolvedFrameGraphBuffer table = resources.GetBuffer(virtualShadowPageTable);
        ValidateVirtualShadowBindings(atlas, table, virtualShadowSampling);
        if (viewIndex >= context.rendererData.views.size())
        {
            FailTransparentScenePass("execution references an out-of-bounds renderer view.");
        }
        const RendererViewData& viewData = context.GetView(viewIndex);
        const std::vector<std::shared_ptr<RTRenderItem>>& items = viewData.transparentItems;
        if (viewData.view.camera == nullptr || items.empty())
        {
            return;
        }
        if (context.rendererData.scene == nullptr)
        {
            FailTransparentScenePass("execution requires a render scene when transparent draws are queued.");
        }

        rhi::RhiCommandList& commandList = context.commandList;

        const UniformBufferAllocation frameUniform = context.frameData.GetFrameUniform(*context.rendererData.scene);
        const rhi::RhiRenderArea& renderArea = context.executionInfo.renderArea;
        const UniformBufferAllocation viewUniform =
            context.frameData.GetViewUniform(viewData.view.camera.get(), rhi::RhiExtent2D{renderArea.width, renderArea.height});
        if (frameUniform.buffer == nullptr || viewUniform.buffer == nullptr)
        {
            FailTransparentScenePass("execution failed to allocate required frame or view uniforms.");
        }
        VirtualShadowGpuConstants virtualShadowConstants = virtualShadowSampling.constants;
        const UniformBufferAllocation virtualShadowUniform = context.frameData.UploadUniform(&virtualShadowConstants, sizeof(virtualShadowConstants));
        if (!virtual_shadow_detail::IsValidVirtualShadowUniformAllocation(virtualShadowUniform))
        {
            FailTransparentScenePass("execution failed to allocate the required VSM uniform.");
        }
        for (SizeT itemIndex = 0; itemIndex < items.size(); ++itemIndex)
        {
            const std::shared_ptr<RTRenderItem>& item = items[itemIndex];
            if (item == nullptr)
            {
                FailTransparentSceneItem(itemIndex, "is null.");
            }

            const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(item->GetMaterialResource());
            if (materialResource == nullptr || materialResource->GetShaderResource() == nullptr ||
                !materialResource->GetShaderResource()->HasPass(ShaderPassType::TransparentForward))
            {
                continue;
            }

            EnsurePipeline(context, viewIndex, materialResource->GetShaderResource());
            commandList.SetPipeline(*pipelineState_);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 0, *frameUniform.buffer, frameUniform.offset, frameUniform.size);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment, 1, *viewUniform.buffer, viewUniform.offset, viewUniform.size);
            BindVirtualShadowSampling(commandList, virtualShadowSampling, virtualShadowUniform);

            const auto meshResource = std::dynamic_pointer_cast<RTMeshResource>(item->GetMeshResource());
            if (meshResource == nullptr)
            {
                FailTransparentSceneItem(itemIndex, "requires an RTMeshResource.");
            }
            if (meshResource->GetVertexBuffer() == nullptr || meshResource->GetVertexStride() == 0 || meshResource->GetVertexCount() == 0)
            {
                FailTransparentSceneItem(itemIndex, "requires a non-empty vertex buffer, stride, and vertex count.");
            }
            if ((meshResource->GetIndexBuffer() == nullptr) != (meshResource->GetIndexCount() == 0))
            {
                FailTransparentSceneItem(itemIndex, "requires its index buffer and index count to be both present or both absent.");
            }

            const UniformBufferAllocation objectUniform = context.frameData.GetObjectUniform(*item);
            if (objectUniform.buffer == nullptr)
            {
                FailTransparentSceneItem(itemIndex, "failed to allocate its object uniform.");
            }
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Vertex, 2, *objectUniform.buffer, objectUniform.offset, objectUniform.size);
            BindMaterialUniform(context, *item);

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
    }

    void TransparentSceneRenderPass::EnsurePipeline(RenderPassContext& context,
                                                    UInt32 viewIndex,
                                                    const std::shared_ptr<RTShaderResource>& shaderResource)
    {
        if (shaderResource == nullptr)
        {
            FailTransparentScenePass("queued draws require an initialized material shader resource.");
        }

        RTShaderPass* shaderPass = shaderResource->GetPass(ShaderPassType::TransparentForward);
        rhi::RhiShaderModule* vertexShader = shaderPass != nullptr ? shaderPass->GetVertexShader() : nullptr;
        rhi::RhiShaderModule* fragmentShader = shaderPass != nullptr ? shaderPass->GetFragmentShader() : nullptr;
        if (vertexShader == nullptr || fragmentShader == nullptr)
        {
            FailTransparentScenePass("material shader resource is missing its vertex or fragment module.");
        }

        const rhi::RhiFillMode fillMode = context.GetView(viewIndex).view.fillMode;
        const rhi::RhiFormat targetFormat = context.executionInfo.colorFormat;
        const bool depthEnabled = context.executionInfo.depthEnabled;
        if (pipelineState_ != nullptr && pipelineColorFormat_ == targetFormat && pipelineFillMode_ == fillMode && pipelineDepthEnabled_ == depthEnabled &&
            pipelineShaderResource_.lock() == shaderResource)
        {
            return;
        }

        RHIPipelineManager* pipelineManager = context.frameData.pipelineManager;
        if (pipelineManager == nullptr)
        {
            FailTransparentScenePass("pipeline creation requires the frame RHIPipelineManager.");
        }

        rhi::RhiVertexAttributeDesc positionAttribute = {};
        positionAttribute.semanticName = "POSITION";
        positionAttribute.semanticIndex = 0;
        positionAttribute.format = rhi::RhiFormat::Rgb32Float;
        positionAttribute.offset = 0;

        rhi::RhiVertexAttributeDesc normalAttribute = {};
        normalAttribute.semanticName = "NORMAL";
        normalAttribute.semanticIndex = 0;
        normalAttribute.format = rhi::RhiFormat::Rgb32Float;
        normalAttribute.offset = sizeof(Float32) * 3;

        const rhi::RhiVertexAttributeDesc vertexAttributes[] = {positionAttribute, normalAttribute};
        const rhi::RhiPipelineResourceBindingDesc resourceBindings[] = {
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 0},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 1},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 1},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 2},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 3},
            {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 4},
            {rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Fragment, 1},
            {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Fragment, 5},
        };
        rhi::RhiGraphicsPipelineDesc pipelineDesc = {};
        pipelineDesc.blendState = rhi::StaticRenderStates::AlphaBlend;
        pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
        pipelineDesc.rasterizerState.fillMode = fillMode;
        pipelineDesc.depthStencilState = depthEnabled ? rhi::StaticRenderStates::DepthReadOnlyLessEqual : rhi::StaticRenderStates::DepthDisabled;
        pipelineDesc.boundShaderState.vertexShader = vertexShader;
        pipelineDesc.boundShaderState.fragmentShader = fragmentShader;
        pipelineDesc.boundShaderState.vertexDeclaration.attributes = vertexAttributes;
        pipelineDesc.boundShaderState.vertexDeclaration.attributeCount = 2;
        pipelineDesc.boundShaderState.vertexDeclaration.stride = sizeof(RTMeshVertex);
        pipelineDesc.resourceLayout.bindings = resourceBindings;
        pipelineDesc.resourceLayout.bindingCount = static_cast<UInt32>(std::size(resourceBindings));
        pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorAttachmentCount = 1;
        pipelineDesc.colorFormat = targetFormat;
        pipelineDesc.debugName = TransparentScenePassName;

        pipelineState_ = pipelineManager->GetOrCreateGraphicsPipeline(
            context.device,
            GraphicsPipelineID{BuildPipelineName(*shaderResource, *vertexShader, *fragmentShader), BuildPipelineVariant(targetFormat, fillMode, depthEnabled)},
            pipelineDesc);
        if (pipelineState_ == nullptr)
        {
            FailTransparentScenePass("failed to create its graphics pipeline.");
        }

        pipelineColorFormat_ = targetFormat;
        pipelineFillMode_ = fillMode;
        pipelineDepthEnabled_ = depthEnabled;
        pipelineShaderResource_ = shaderResource;
    }

    void TransparentSceneRenderPass::BindMaterialUniform(RenderPassContext& context, const RTRenderItem& item)
    {
        const auto materialResource = std::dynamic_pointer_cast<RTMaterialResource>(item.GetMaterialResource());
        if (materialResource == nullptr)
        {
            FailTransparentScenePass("queued render item requires an RTMaterialResource.");
        }
        if (materialResource->GetShaderResource() == nullptr)
        {
            FailTransparentScenePass("queued render item material requires an initialized shader resource.");
        }
        if (materialResource->GetUniformBuffer() == nullptr || materialResource->GetUniformBufferSize() == 0)
        {
            FailTransparentScenePass("queued render item material requires an initialized uniform buffer.");
        }
        context.commandList.SetUniformBuffer(rhi::RhiShaderStage::Fragment,
                                             3,
                                             *materialResource->GetUniformBuffer(),
                                             materialResource->GetUniformBufferOffset(),
                                             materialResource->GetUniformBufferSize());
    }

} // namespace ve
