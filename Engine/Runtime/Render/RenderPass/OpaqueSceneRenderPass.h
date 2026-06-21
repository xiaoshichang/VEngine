#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Render/BaseRenderer.h"
#include "Engine/Runtime/Render/RenderPass/RenderPass.h"

#include <memory>
#include <vector>

namespace ve
{
    struct RTRenderItemDesc;
    class RTScene;
    class RTShaderResource;

    class OpaqueSceneRenderPass final : public RenderPass
    {
    public:
        OpaqueSceneRenderPass(RendererRenderTarget target, rhi::RhiFillMode fillMode);

        [[nodiscard]] const char* GetName() const noexcept override;
        void Setup(RenderPassBuilder& builder) override;
        void Execute(RenderPassContext& context) override;

    private:
        void EnsurePipeline(RenderPassContext& context);
        void BindLightUniform(RenderPassContext& context, const RTScene& scene);
        [[nodiscard]] bool BindMaterialUniform(RenderPassContext& context, const RTRenderItemDesc& itemDesc);
        [[nodiscard]] rhi::RhiFormat ResolveTargetFormat(const RenderPassContext& context) const noexcept;

        RendererRenderTarget target_;
        rhi::RhiFillMode fillMode_ = rhi::RhiFillMode::Solid;
        std::unique_ptr<rhi::RhiPipelineState> pipelineState_;
        std::vector<std::unique_ptr<rhi::RhiBuffer>> frameUniformBuffers_;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        rhi::RhiFillMode pipelineFillMode_ = rhi::RhiFillMode::Solid;
        std::weak_ptr<RTShaderResource> pipelineShaderResource_;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
