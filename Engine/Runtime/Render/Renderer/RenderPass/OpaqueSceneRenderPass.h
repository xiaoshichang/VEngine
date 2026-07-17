#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <memory>

namespace ve
{
    class RTShaderResource;

    struct OpaqueSceneRenderPassInitParam
    {
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
        rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
    };

    class OpaqueSceneRenderPass final : public RenderPass
    {
    public:
        explicit OpaqueSceneRenderPass(OpaqueSceneRenderPassInitParam initParam);

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

    private:
        [[nodiscard]] ErrorCode Draw(RenderPassContext& context);
        [[nodiscard]] ErrorCode EnsurePipeline(RenderPassContext& context);
        [[nodiscard]] bool BindMaterialUniform(RenderPassContext& context, const RTRenderItem& item);
        OpaqueSceneRenderPassInitParam initParam_;
        rhi::RhiPipelineState* pipelineState_ = nullptr;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        rhi::RhiFillMode pipelineFillMode_ = rhi::RhiFillMode::Solid;
        std::weak_ptr<RTShaderResource> pipelineShaderResource_;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
