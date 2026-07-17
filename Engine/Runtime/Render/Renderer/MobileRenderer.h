#pragma once

#include "Engine/Runtime/Render/Renderer/BaseRenderer.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h"

namespace ve
{
    struct MobileRendererInitParam : public BaseRendererInitParam
    {
    };

    /// Mobile renderer owns an independent topology without editor pass injection.
    class MobileRenderer final : public BaseRenderer
    {
    public:
        explicit MobileRenderer(MobileRendererInitParam initParam);

    private:
        void BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

        OpaqueSceneRenderPass opaquePass_;
        TransparentSceneRenderPass transparentPass_;
    };
} // namespace ve
