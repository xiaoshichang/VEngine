#include "Engine/Runtime/Render/Renderer/MobileRenderer.h"

namespace ve
{
    MobileRenderer::MobileRenderer(MobileRendererInitParam initParam)
        : BaseRenderer(static_cast<const BaseRendererInitParam&>(initParam))
        , opaquePass_(OpaqueSceneRenderPassInitParam{initParam.fillMode, initParam.target.colorLoadAction, initParam.target.colorStoreAction})
        , transparentPass_(TransparentSceneRenderPassInitParam{initParam.fillMode, initParam.target.colorStoreAction})
    {
    }

    void MobileRenderer::BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        opaquePass_.AddToFrameGraph(frameGraph, graphData);
        transparentPass_.AddToFrameGraph(frameGraph, graphData);
    }
} // namespace ve
