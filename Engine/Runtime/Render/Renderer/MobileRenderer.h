#pragma once

#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/OpaqueForwardPass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h"

#include <memory>
#include <vector>

namespace ve
{
    struct MobileRendererInitParam : public BaseRendererInitParam
    {
    };

    /// Mobile renderer owns an independent family topology without editor pass injection.
    class MobileRenderer final : public BaseRenderer
    {
    public:
        explicit MobileRenderer(MobileRendererInitParam initParam);

    private:
        void BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

        bool hasDepthPrePass_ = false;
        DepthPrePass depthPrePass_;
        std::vector<std::unique_ptr<OpaqueForwardPass>> opaquePasses_;
        TransparentSceneRenderPass transparentPass_;
    };
} // namespace ve
