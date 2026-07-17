#include "Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h"

#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"

namespace ve
{
    namespace
    {
        struct TransparentScenePassData
        {
            FrameGraphTextureHandle color;
            FrameGraphTextureHandle depth;
        };
    } // namespace

    TransparentSceneRenderPass::TransparentSceneRenderPass(TransparentSceneRenderPassInitParam initParam)
        : initParam_(initParam)
        , executor_(SceneRenderPassSettings{"TransparentScenePass", initParam.fillMode, true})
    {
    }

    void TransparentSceneRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        frameGraph.AddRasterPass<TransparentScenePassData>(
            "TransparentScenePass",
            [this, &graphData](FrameGraphBuilder& builder, TransparentScenePassData& passData)
            {
                passData.color = builder.Write(graphData.color, FrameGraphTextureAccess::ColorAttachment);
                builder.SetColorAttachment(
                    passData.color, rhi::RhiLoadAction::Load, initParam_.colorStoreAction, rhi::RhiColor{});
                graphData.color = passData.color;

                if (graphData.depth.IsValid())
                {
                    passData.depth = builder.Read(graphData.depth, FrameGraphTextureAccess::DepthAttachment);
                    builder.SetDepthAttachment(passData.depth,
                                               rhi::RhiLoadAction::Load,
                                               rhi::RhiStoreAction::Store,
                                               rhi::RhiDepthStencilClearValue{},
                                               true);
                }
            },
            [this](const TransparentScenePassData&, RenderPassContext& context)
            { return executor_.Draw(context, context.rendererData.transparentItems); });
    }
} // namespace ve
