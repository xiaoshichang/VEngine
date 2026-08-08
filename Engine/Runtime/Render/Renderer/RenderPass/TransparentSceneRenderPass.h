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
    };

    class TransparentSceneRenderPass final : public ViewRenderPass
    {
    public:
        explicit TransparentSceneRenderPass(TransparentSceneRenderPassInitParam initParam);

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) override;

    private:
        void Draw(const FrameGraphPassResources& resources,
                  FrameGraphTextureHandle virtualShadowAtlas,
                  FrameGraphBufferHandle virtualShadowPageTable,
                  const VirtualShadowSamplingSnapshot& virtualShadowSampling,
                  UInt32 viewIndex,
                  RenderPassContext& context);
        void EnsurePipeline(RenderPassContext& context, UInt32 viewIndex, const std::shared_ptr<RTShaderResource>& shaderResource);
        void BindMaterialUniform(RenderPassContext& context, const RTRenderItem& item);
        TransparentSceneRenderPassInitParam initParam_;
        rhi::RhiPipelineState* pipelineState_ = nullptr;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        rhi::RhiFillMode pipelineFillMode_ = rhi::RhiFillMode::Solid;
        std::weak_ptr<RTShaderResource> pipelineShaderResource_;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
