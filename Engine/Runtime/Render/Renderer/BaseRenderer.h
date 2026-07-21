#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/RenderTexture.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <memory>

namespace ve
{
    class FrameGraph;

    /// Describes where a renderer writes its scene color and how the first scene pass treats it.
    struct RendererRenderTarget
    {
        std::shared_ptr<RTRenderTexture> colorTexture;
        rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
    };

    struct BaseRendererInitParam
    {
        const FrameRenderPipelineData* frameData = nullptr;
        std::shared_ptr<RTScene> scene;
        std::shared_ptr<RTCamera> camera;
        std::shared_ptr<RTRenderViewState> viewState;
        RendererRenderTarget target;
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
    };

    /// Builds one view's queues and executes the concrete renderer topology through a compiled frame graph.
    class BaseRenderer : public NonCopyable
    {
    public:
        virtual ~BaseRenderer() = default;

        [[nodiscard]] ErrorCode RenderScene();

    protected:
        explicit BaseRenderer(BaseRendererInitParam initParam);

        virtual void BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) = 0;
        [[nodiscard]] const RendererData& GetRendererData() const noexcept;

    private:
        void UpdateRenderWorld();
        void BuildRenderQueues();
        void PrepareVirtualShadows();
        [[nodiscard]] ErrorCode ImportRenderTargets(FrameGraph& frameGraph, RendererFrameGraphData& graphData) const;
        void ImportVirtualShadowResources(FrameGraph& frameGraph, RendererFrameGraphData& graphData) const;

        RendererRenderTarget target_;
        RendererData rendererData_ = {};
        const FrameRenderPipelineData* frameRenderData_ = nullptr;
    };
} // namespace ve
