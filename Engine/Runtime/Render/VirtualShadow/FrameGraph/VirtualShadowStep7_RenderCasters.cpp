#include "Engine/RHI/Common/RhiStaticStates.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderResource.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/RenderShaderResources.h"
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
        [[nodiscard]] rhi::RhiPipelineState* GetStep7_RenderCastersPipeline(const FrameRenderPipelineData& frameData)
        {
            if (rhi::RhiPipelineState* cached = frameData.shaderManager->GetGraphicsPipeline(GraphicsPipelineID{"VirtualShadowStep7_RenderCasters", 0}); cached != nullptr)
            {
                return cached;
            }
            if (frameData.shaderResources == nullptr || frameData.shaderResources->virtualShadow == nullptr)
            {
                FailVirtualShadow("VSM render shader resource is unavailable.");
            }
            const RTShaderPass* shaderPass = frameData.shaderResources->virtualShadow->GetPass("VirtualShadowStep7_RenderCasters");
            if (shaderPass == nullptr || shaderPass->GetVertexShader() == nullptr || shaderPass->GetFragmentShader() == nullptr)
            {
                FailVirtualShadow("VSM Step7_RenderCasters shader pass is unavailable.");
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
            desc.boundShaderState.vertexShader = shaderPass->GetVertexShader();
            desc.boundShaderState.fragmentShader = shaderPass->GetFragmentShader();
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
                const std::shared_ptr<RTMaterialResource> material = std::dynamic_pointer_cast<RTMaterialResource>(item->GetMaterialResource());
                if (material == nullptr || material->GetShaderResource() == nullptr ||
                    !material->GetShaderResource()->HasPass(ShaderPassType::ShadowCaster))
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
