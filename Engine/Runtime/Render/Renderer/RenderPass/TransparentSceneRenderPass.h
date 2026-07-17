#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

namespace ve
{
    struct TransparentSceneRenderPassInitParam
    {
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
        rhi::RhiStoreAction colorStoreAction = rhi::RhiStoreAction::Store;
    };

    class TransparentSceneRenderPass final : public RenderPass
    {
    public:
        explicit TransparentSceneRenderPass(TransparentSceneRenderPassInitParam initParam);

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

    private:
        TransparentSceneRenderPassInitParam initParam_;
        SceneRenderPassExecutor executor_;
    };
} // namespace ve
