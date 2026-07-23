#include "Engine/Runtime/Render/Renderer/MobileRenderer.h"

namespace ve
{
    MobileRenderer::MobileRenderer(MobileRendererInitParam initParam)
        : BaseRenderer(static_cast<const BaseRendererInitParam&>(initParam))
        , opaquePass_(OpaqueSceneRenderPassInitParam{initParam.fillMode, initParam.target.colorLoadAction})
        , transparentPass_(TransparentSceneRenderPassInitParam{initParam.fillMode})
    {
    }

    void MobileRenderer::BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        gpuVirtualShadowPass_.AddToFrameGraph(frameGraph, graphData);
        virtualShadowDepthPass_.AddToFrameGraph(frameGraph, graphData);
        opaquePass_.AddToFrameGraph(frameGraph, graphData);
        transparentPass_.AddToFrameGraph(frameGraph, graphData);
    }
} // namespace ve
