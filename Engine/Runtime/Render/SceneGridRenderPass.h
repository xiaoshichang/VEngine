#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderPass.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <memory>
#include <vector>

namespace ve
{
    struct SceneGridRenderPassDesc
    {
        std::shared_ptr<RTRenderTexture> colorTexture;
        Float32 opacity = 0.45f;
        Float32 unitSize = 1.0f;
    };

    class SceneGridRenderPass final : public RenderPass
    {
    public:
        explicit SceneGridRenderPass(SceneGridRenderPassDesc desc);

        [[nodiscard]] const char* GetName() const noexcept override;
        void Setup(RenderPassBuilder& builder) override;
        void Execute(RenderPassContext& context) override;

    private:
        void EnsureResources(RenderPassContext& context);
        void EnsurePipeline(RenderPassContext& context);
        void UploadUniforms(RenderPassContext& context);

        SceneGridRenderPassDesc desc_;
        std::unique_ptr<rhi::RhiBuffer> vertexBuffer_;
        std::unique_ptr<rhi::RhiBuffer> uniformBuffer_;
        std::unique_ptr<rhi::RhiPipelineState> pipelineState_;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
