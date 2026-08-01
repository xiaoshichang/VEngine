#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

namespace ve
{
    class FrameGraphPassResources;

    class ShadowCasterDirtyDebugPass final : public ViewRenderPass
    {
    public:
        ShadowCasterDirtyDebugPass() = default;

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) override;

    private:
        void Draw(const FrameGraphPassResources& resources,
                  FrameGraphTextureHandle virtualShadowAtlas,
                  FrameGraphBufferHandle virtualShadowPageTable,
                  const VirtualShadowSamplingSnapshot& virtualShadowSampling,
                  UInt32 viewIndex,
                  RenderPassContext& context);
        void EnsurePipeline(RenderPassContext& context, UInt32 viewIndex);

        rhi::RhiPipelineState* pipelineState_ = nullptr;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        rhi::RhiFillMode pipelineFillMode_ = rhi::RhiFillMode::Solid;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
