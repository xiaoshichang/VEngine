#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h"
#include "Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPasses.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowError.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h"

#include <iterator>
#include <string>

namespace ve
{
    namespace
    {
        struct Step1_ClearScenePassData
        {
            VirtualShadowSceneCache* sceneCache = nullptr;
            FrameGraphBufferHandle physicalPages;
            FrameGraphBufferHandle statistics;
            bool resetSceneCache = false;
        };

        [[nodiscard]] VirtualShadowGpuConstants BuildSceneConstants(const VirtualShadowSceneCache& sceneCache, UInt64 frameIndex, bool resetSceneCache) noexcept
        {
            VirtualShadowGpuConstants constants = {};
            constants.atlasExtent = sceneCache.GetDesc().atlasExtent;
            constants.physicalPageCapacity = sceneCache.GetPhysicalPageCapacity();
            constants.frameIndex = static_cast<UInt32>(frameIndex);
            constants.resetCache = resetSceneCache ? 1u : 0u;
            return constants;
        }

        void RecordStep1_ClearScene(const FrameRenderPipelineData& frameData,
                                    VirtualShadowSceneCache& sceneCache,
                                    bool resetSceneCache,
                                    rhi::RhiBuffer& physicalPages,
                                    rhi::RhiBuffer& statistics)
        {
            const rhi::RhiPipelineResourceBindingDesc bindings[] = {
                {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Compute, 4},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 0},
                {rhi::RhiPipelineResourceKind::ReadWriteStorageBuffer, rhi::RhiShaderStage::Compute, 1},
            };
            rhi::RhiComputePipelineState* pipeline = virtual_shadow_detail::GetVirtualShadowComputePipeline(frameData,
                                                                                                            "VirtualShadowStep1_ClearScene",
                                                                                                            bindings,
                                                                                                            static_cast<UInt32>(std::size(bindings)));
            if (pipeline == nullptr)
            {
                FailVirtualShadow("VSM Step1_ClearScene requires its compute pipeline.");
            }

            const UniformBufferAllocation constants =
                virtual_shadow_detail::UploadVirtualShadowPassConstants(frameData, BuildSceneConstants(sceneCache, frameData.frameIndex, resetSceneCache));
            rhi::RhiCommandList& commandList = frameData.GetCommandList();
            commandList.SetComputePipeline(*pipeline);
            commandList.SetUniformBuffer(rhi::RhiShaderStage::Compute, 4, *constants.buffer, constants.offset, constants.size);
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 0, physicalPages, 0, physicalPages.GetSize());
            commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute, 1, statistics, 0, statistics.GetSize());
            commandList.Dispatch(virtual_shadow_detail::GetVirtualShadowPhysicalPageDispatchGroupCount(sceneCache.GetPhysicalPageCapacity()), 1, 1);
        }
    } // namespace

    void AddVirtualShadowStep1_ClearScenePass(FrameGraph& graph, VirtualShadowPreparedFamilyGraphData& family, VirtualShadowFrameGraphResources& resources)
    {
        if (family.sceneCache == nullptr || family.views.empty())
        {
            FailVirtualShadow("VSM Step1_ClearScene registration requires a non-empty prepared family.");
        }
        const bool resetSceneCache = family.views.front().packet.resetSceneCache;
        virtual_shadow_detail::ValidateVirtualShadowPhysicalCapacity(resources.physicalPageCapacity);
        if (resources.physicalPageCapacity != family.sceneCache->GetPhysicalPageCapacity())
        {
            FailVirtualShadow("VSM graph resources must preserve the prepared scene cache's physical-page capacity.");
        }
        for (const VirtualShadowPreparedViewGraphData& view : family.views)
        {
            virtual_shadow_detail::ValidateVirtualShadowRequestView(view, false);
            if (view.packet.resetSceneCache != resetSceneCache)
            {
                FailVirtualShadow("VSM prepared family must agree on scene-cache reset state.");
            }
        }

        VirtualShadowSceneCache* sceneCache = family.sceneCache;
        graph.AddComputePass<Step1_ClearScenePassData>(
            "VirtualShadow.Step1_ClearScene",
            [&resources, &family, sceneCache, resetSceneCache](FrameGraphBuilder& builder, Step1_ClearScenePassData& data)
            {
                data.sceneCache = sceneCache;
                data.resetSceneCache = resetSceneCache;
                data.physicalPages = builder.Write(resources.physicalPages);
                resources.physicalPages = data.physicalPages;
                family.physicalPages = data.physicalPages;
                data.statistics = builder.Write(resources.statistics);
                resources.statistics = data.statistics;
                family.statistics = data.statistics;
            },
            [](const Step1_ClearScenePassData& data, const FrameGraphPassResources& passResources, RenderPassContext& context)
            {
                RecordStep1_ClearScene(context.frameData,
                                       *data.sceneCache,
                                       data.resetSceneCache,
                                       virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.physicalPages),
                                       virtual_shadow_detail::RequireVirtualShadowBuffer(passResources, data.statistics));
            });
    }
} // namespace ve
