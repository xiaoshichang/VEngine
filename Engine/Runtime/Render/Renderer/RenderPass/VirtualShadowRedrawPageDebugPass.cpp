#include "Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowRedrawPageDebugPass.h"

#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Render/RHIPipelineManager.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/RenderUniformBuffer.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"
#include "Engine/Runtime/Resource/BuiltInShaderLibrary.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <exception>
#include <iterator>
#include <string>

namespace ve
{
    namespace
    {
        inline constexpr const char* DebugPassName = "VirtualShadowRedrawPageDebugPass";

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

            const UInt64 requiredPhysicalPageSize = static_cast<UInt64>(sampling.constants.physicalPageCapacity) * sizeof(VirtualShadowGpuPhysicalPage);
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

        const UniformBufferAllocation frameUniform = context.frameData.GetSceneUniform(*context.rendererData.scene);
        const rhi::RhiRenderArea& renderArea = context.executionInfo.renderArea;
        const UniformBufferAllocation viewUniform =
            context.frameData.GetViewUniform(*viewData.view.viewState, viewData.view.camera.get(), rhi::RhiExtent2D{renderArea.width, renderArea.height});
        const VirtualShadowGpuConstants virtualShadowConstants = virtualShadowSampling.constants;
        const UniformBufferAllocation virtualShadowUniform =
            context.frameData.UploadTransientUniform(&virtualShadowConstants, sizeof(virtualShadowConstants), "VirtualShadowSamplingUniform");
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

        RHIPipelineManager* pipelineManager = context.frameData.pipelineManager;
        if (pipelineManager == nullptr)
        {
            FailDebugPass("pipeline creation requires the frame RHIPipelineManager.");
        }

        if (context.frameData.builtInShaderResources == nullptr || context.frameData.builtInShaderResources->virtualShadowRedrawPageDebug == nullptr)
        {
            FailDebugPass("the VirtualShadowRedrawPageDebug shader resource is unavailable.");
        }
        const RTShaderPass* shaderPass = context.frameData.builtInShaderResources->virtualShadowRedrawPageDebug->GetPass("Internal");
        if (shaderPass == nullptr || shaderPass->GetVertexShader() == nullptr || shaderPass->GetFragmentShader() == nullptr)
        {
            FailDebugPass("the VirtualShadowRedrawPageDebug shader pass is unavailable.");
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
        pipelineDesc.boundShaderState.vertexShader = shaderPass->GetVertexShader();
        pipelineDesc.boundShaderState.fragmentShader = shaderPass->GetFragmentShader();
        pipelineDesc.boundShaderState.vertexDeclaration = {attributes, static_cast<UInt32>(std::size(attributes)), sizeof(RTMeshVertex)};
        pipelineDesc.resourceLayout = {bindings, static_cast<UInt32>(std::size(bindings))};
        pipelineDesc.primitiveType = rhi::RhiPrimitiveTopology::TriangleList;
        pipelineDesc.colorAttachmentCount = 1;
        pipelineDesc.colorFormat = targetFormat;
        pipelineDesc.depthFormat = depthEnabled ? rhi::RhiFormat::Depth32Float : rhi::RhiFormat::Unknown;
        pipelineDesc.debugName = DebugPassName;

        pipelineState_ = pipelineManager->GetOrCreateGraphicsPipeline(
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
