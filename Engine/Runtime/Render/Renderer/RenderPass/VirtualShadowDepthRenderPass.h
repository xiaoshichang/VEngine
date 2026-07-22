#pragma once

#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

namespace ve
{
    class RTShaderResource;
    struct VirtualShadowFramePacket;

    class VirtualShadowDepthRenderPass final : public RenderPass
    {
    public:
        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

    private:
        [[nodiscard]] ErrorCode Draw(RenderPassContext& context);
        [[nodiscard]] rhi::RhiPipelineState* GetClearPipeline(RenderPassContext& context);
        [[nodiscard]] rhi::RhiPipelineState*
        GetCasterPipeline(RenderPassContext& context, RTShaderResource& shaderResource);
    };
} // namespace ve
