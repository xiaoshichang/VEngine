#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <memory>

namespace ve
{
    class FrameGraphPassResources;
    class RTShaderResource;

    struct TransparentSceneRenderPassInitParam
    {
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
    };

    class TransparentSceneRenderPass final : public RenderPass
    {
    public:
        explicit TransparentSceneRenderPass(TransparentSceneRenderPassInitParam initParam);

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;

    private:
        [[nodiscard]] ErrorCode Draw(const FrameGraphPassResources& resources,
                                     FrameGraphTextureHandle virtualShadowAtlas,
                                     FrameGraphBufferHandle virtualShadowPageTable,
                                     RenderPassContext& context);
        [[nodiscard]] ErrorCode EnsurePipeline(RenderPassContext& context);
        [[nodiscard]] bool BindMaterialUniform(RenderPassContext& context, const RTRenderItem& item);
        TransparentSceneRenderPassInitParam initParam_;
        rhi::RhiPipelineState* pipelineState_ = nullptr;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        rhi::RhiFillMode pipelineFillMode_ = rhi::RhiFillMode::Solid;
        std::weak_ptr<RTShaderResource> pipelineShaderResource_;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
