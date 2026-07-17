#include "Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Render/RenderScene.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"

#include <utility>

namespace ve
{
    namespace
    {
        struct OpaqueScenePassData
        {
            FrameGraphTextureHandle color;
            FrameGraphTextureHandle depth;
        };
    } // namespace

    OpaqueSceneRenderPass::OpaqueSceneRenderPass(OpaqueSceneRenderPassInitParam initParam)
        : initParam_(initParam)
        , executor_(SceneRenderPassSettings{"OpaqueScenePass", initParam.fillMode, false})
    {
    }

    void OpaqueSceneRenderPass::AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
    {
        frameGraph.AddRasterPass<OpaqueScenePassData>(
            "OpaqueScenePass",
            [this, &graphData](FrameGraphBuilder& builder, OpaqueScenePassData& passData)
            {
                rhi::RhiColor clearColor = {};
                if (builder.GetRendererData().resolvedCamera != nullptr)
                {
                    clearColor = builder.GetRendererData().resolvedCamera->GetClearColor();
                }

                passData.color = builder.Write(graphData.color, FrameGraphTextureAccess::ColorAttachment);
                builder.SetColorAttachment(passData.color, initParam_.colorLoadAction, initParam_.colorStoreAction, clearColor);
                graphData.color = passData.color;

                if (graphData.depth.IsValid())
                {
                    passData.depth = builder.Write(graphData.depth, FrameGraphTextureAccess::DepthAttachment);
                    builder.SetDepthAttachment(passData.depth,
                                               rhi::RhiLoadAction::Clear,
                                               rhi::RhiStoreAction::Store,
                                               rhi::RhiDepthStencilClearValue{},
                                               false);
                    graphData.depth = passData.depth;
                }
            },
            [this](const OpaqueScenePassData&, RenderPassContext& context)
            { return executor_.Draw(context, context.rendererData.opaqueItems); });
    }
} // namespace ve
