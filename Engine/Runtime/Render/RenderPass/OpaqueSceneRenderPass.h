#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Render/BaseRenderer.h"
#include "Engine/Runtime/Render/RenderPass/RenderPass.h"

#include <memory>
#include <vector>

namespace ve
{
    class RTRenderItem;
    class RTScene;
    class RTShaderResource;

    struct OpaqueSceneRenderPassInitParam
    {
        RendererRenderTarget target;
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
    };

    class OpaqueSceneRenderPass final : public RenderPass
    {
    public:
        explicit OpaqueSceneRenderPass(OpaqueSceneRenderPassInitParam initParam);

        [[nodiscard]] const char* GetName() const noexcept override;
        void Setup(RenderPassBuilder& builder) override;
        void Execute(RenderPassContext& context) override;

    private:
        void EnsurePipeline(RenderPassContext& context);
        void BindLightUniform(RenderPassContext& context, const RTScene& scene);
        [[nodiscard]] bool BindMaterialUniform(RenderPassContext& context, const RTRenderItem& item);
        [[nodiscard]] rhi::RhiFormat ResolveTargetFormat(const RenderPassContext& context) const noexcept;

        OpaqueSceneRenderPassInitParam initParam_;
        std::unique_ptr<rhi::RhiPipelineState> pipelineState_;
        std::vector<std::unique_ptr<rhi::RhiBuffer>> frameUniformBuffers_;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        rhi::RhiFillMode pipelineFillMode_ = rhi::RhiFillMode::Solid;
        std::weak_ptr<RTShaderResource> pipelineShaderResource_;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
