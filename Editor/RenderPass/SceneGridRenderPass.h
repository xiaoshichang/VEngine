#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <memory>
#include <vector>

namespace ve
{
    struct SceneGridRenderPassInitParam
    {
        Float32 opacity = 0.45f;
        Float32 unitSize = 1.0f;
    };

    class SceneGridRenderPass final : public ViewRenderPass
    {
    public:
        explicit SceneGridRenderPass(SceneGridRenderPassInitParam initParam);

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) override;

    private:
        void Execute(RenderPassContext& context, UInt32 viewIndex);
        void EnsureResources(RenderPassContext& context);
        void EnsurePipeline(RenderPassContext& context);

        SceneGridRenderPassInitParam initParam_;
        std::unique_ptr<rhi::RhiBuffer> vertexBuffer_;
        rhi::RhiGraphicsPipelineState* pipelineState_ = nullptr;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
