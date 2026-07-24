#include "Engine/Runtime/Render/Renderer/StandaloneRenderer.h"

#include "Engine/Runtime/Core/Assert.h"

#include <utility>

namespace ve
{
    StandaloneRenderer::StandaloneRenderer(StandaloneRendererInitParam initParam)
        : BaseRenderer(static_cast<const BaseRendererInitParam&>(initParam))
        , opaquePass_(OpaqueSceneRenderPassInitParam{initParam.fillMode, initParam.target.colorLoadAction})
        , transparentPass_(TransparentSceneRenderPassInitParam{initParam.fillMode})
        , additionalPasses_(std::move(initParam.additionalPasses))
    {
    }

    void StandaloneRenderer::BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        gpuVirtualShadowPass_.AddToFrameGraph(frameGraph, graphData);
        opaquePass_.AddToFrameGraph(frameGraph, graphData);
        transparentPass_.AddToFrameGraph(frameGraph, graphData);
        for (std::unique_ptr<RenderPass>& pass : additionalPasses_)
        {
            VE_ASSERT(pass != nullptr);
            pass->AddToFrameGraph(frameGraph, graphData);
        }
    }
} // namespace ve
