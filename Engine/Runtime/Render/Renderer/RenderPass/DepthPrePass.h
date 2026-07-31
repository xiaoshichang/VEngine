#pragma once

#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

namespace ve
{
    /// Writes opaque receiver depth for one family view.
    /// Resource contract:
    /// - reads RendererData::opaqueItems and indexed view camera;
    /// - writes only RendererFrameGraphData::views[viewIndex].depth;
    /// - produces depth consumed by VSM request marking and Opaque.
    class DepthPrePass final : public ViewRenderPass
    {
    public:
        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) override;

    private:
        void Draw(UInt32 viewIndex, RenderPassContext& context);
    };
} // namespace ve
