#pragma once

#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.h"

#include <memory>
#include <vector>

namespace ve
{
    struct StandaloneRendererInitParam : public BaseRendererInitParam
    {
        std::vector<std::unique_ptr<RenderPass>> additionalPasses;
    };

    /// Host-platform renderer with scene passes followed by optional editor passes.
    class StandaloneRenderer final : public BaseRenderer
    {
    public:
        explicit StandaloneRenderer(StandaloneRendererInitParam initParam);

    private:
        void BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

        VirtualShadowDepthRenderPass virtualShadowDepthPass_;
        OpaqueSceneRenderPass opaquePass_;
        TransparentSceneRenderPass transparentPass_;
        std::vector<std::unique_ptr<RenderPass>> additionalPasses_;
    };
} // namespace ve
