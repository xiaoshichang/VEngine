#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

namespace ve
{
    struct OpaqueSceneRenderPassInitParam
    {
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
        rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
        rhi::RhiStoreAction colorStoreAction = rhi::RhiStoreAction::Store;
    };

    class OpaqueSceneRenderPass final : public RenderPass
    {
    public:
        explicit OpaqueSceneRenderPass(OpaqueSceneRenderPassInitParam initParam);

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

    private:
        OpaqueSceneRenderPassInitParam initParam_;
        SceneRenderPassExecutor executor_;
    };
} // namespace ve
