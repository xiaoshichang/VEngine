#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/ShaderManager.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"

#include <iterator>
#include <string>

namespace ve
{
    namespace
    {
        [[nodiscard]] rhi::RhiComputePipelineState* GetStep6_ClearPhysicalPagesPipeline(const FrameRenderPipelineData& frameData)
        {
            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
                {rhi::RhiPipelineResourceKind::StorageBuffer, rhi::RhiShaderStage::Compute, 2},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageTexture, rhi::RhiShaderStage::Compute, 0},
            };
            return virtual_shadow_detail::GetVirtualShadowComputePipeline(frameData,
                                                                          "VirtualShadowStep6_ClearPhysicalPages",
                                                                          "VirtualShadow.Step6_ClearPhysicalPages.Compute",
                                                                          bindings,
                                                                          static_cast<UInt32>(std::size(bindings)));
        }

        void RecordStep6_ClearPhysicalPages(const VirtualShadowPageRecordingContext& context, rhi::RhiBuffer& physicalPages, rhi::RhiTexture& atlas)
        {
            rhi::RhiComputePipelineState* pipeline = GetStep6_ClearPhysicalPagesPipeline(context.frameData);
            if (pipeline == nullptr)
            {
                FailVirtualShadow("VSM Step6_ClearPhysicalPages requires its compute pipeline.");
            }

            const UniformBufferAllocation constants =
                virtual_shadow_detail::UploadVirtualShadowPassConstants(context.frameData, virtual_shadow_detail::BuildVirtualShadowPageGpuConstants(context));
            rhi::RhiCommandList& commandList = context.frameData.GetCommandList();
            commandList.SetComputePipeline(*pipeline);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
            commandList.SetStorageBuffer(rhi::RhiShaderStage::Compute, 2, physicalPages, 0, physicalPages.GetSize());
            commandList.SetReadWriteStorageTexture(rhi::RhiShaderStage::Compute, 0, atlas);
            commandList.Dispatch(context.physicalPageCapacity, 1, 1);
        }
    } // namespace

    void AddVirtualShadowStep6_ClearPhysicalPagesPass(FrameGraph& frameGraph,
                                                      const VirtualShadowPreparedViewGraphData& view,
                                                      VirtualShadowFrameGraphResources& resources,
                                                      bool atlasUavBarrierBeforeExecute)
    {
        virtual_shadow_detail::ValidateVirtualShadowPageView(view, resources);
        frameGraph.AddComputePass<virtual_shadow_detail::VirtualShadowViewPagePassData>(
            "VirtualShadow.Step6_ClearPhysicalPages[View=" + std::to_string(view.packet.viewID) + "]",
            [&resources, view, atlasUavBarrierBeforeExecute](FrameGraphBuilder& builder, virtual_shadow_detail::VirtualShadowViewPagePassData& data)
            {
                data.viewIndex = view.viewIndex;
                data.packet = view.packet;
                data.physicalPageCapacity = resources.physicalPageCapacity;
                data.physicalPages = builder.Read(resources.physicalPages);
                data.atlas = builder.Write(resources.atlas);
                resources.atlas = data.atlas;
                if (atlasUavBarrierBeforeExecute)
                {
                    builder.AddUavBarrierBeforeExecute(data.atlas);
                }
            },
            [](const virtual_shadow_detail::VirtualShadowViewPagePassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            {
                static_cast<void>(context.GetView(data.viewIndex));
                RecordStep6_ClearPhysicalPages(virtual_shadow_detail::BuildVirtualShadowPagePassContext(data, context),
                                               virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.physicalPages),
                                               virtual_shadow_detail::RequireVirtualShadowTexture(passResources, data.atlas));
            });
    }
} // namespace ve
