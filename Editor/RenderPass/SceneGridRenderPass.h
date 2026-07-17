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

    class SceneGridRenderPass final : public RenderPass
    {
    public:
        explicit SceneGridRenderPass(SceneGridRenderPassInitParam initParam);

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

    private:
        [[nodiscard]] ErrorCode Execute(RenderPassContext& context);
        void EnsureResources(RenderPassContext& context);
        void EnsurePipeline(RenderPassContext& context);

        SceneGridRenderPassInitParam initParam_;
        std::unique_ptr<rhi::RhiBuffer> vertexBuffer_;
        rhi::RhiPipelineState* pipelineState_ = nullptr;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
