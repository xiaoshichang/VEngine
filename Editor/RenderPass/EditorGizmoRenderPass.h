#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderPass/RenderPass.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <memory>
#include <vector>

namespace ve
{
    struct EditorGizmoVertex
    {
        Float32 position[3] = {};
        Float32 color[3] = {};
    };

    struct EditorGizmoIconVertex
    {
        Float32 position[3] = {};
        Float32 uv[3] = {};
        Float32 color[3] = {};
    };

    struct EditorGizmoDrawList
    {
        std::vector<EditorGizmoVertex> lines;
        std::vector<EditorGizmoIconVertex> icons;
    };

    struct EditorGizmoRenderPassInitParam
    {
        std::shared_ptr<RTRenderTexture> colorTexture;
        std::shared_ptr<const EditorGizmoDrawList> drawList;
    };

    class EditorGizmoRenderPass final : public RenderPass
    {
    public:
        explicit EditorGizmoRenderPass(EditorGizmoRenderPassInitParam initParam);

        [[nodiscard]] const char* GetName() const noexcept override;
        void Setup(RenderPassBuilder& builder) override;
        void Execute(RenderPassContext& context) override;

    private:
        void EnsurePipeline(RenderPassContext& context);
        void EnsureIconResources(RenderPassContext& context);
        void UploadFrameResources(RenderPassContext& context);
        [[nodiscard]] rhi::RhiFormat ResolveTargetFormat(const RenderPassContext& context) const noexcept;

        EditorGizmoRenderPassInitParam initParam_;
        std::unique_ptr<rhi::RhiBuffer> lineVertexBuffer_;
        std::unique_ptr<rhi::RhiBuffer> iconVertexBuffer_;
        std::unique_ptr<rhi::RhiBuffer> uniformBuffer_;
        std::unique_ptr<rhi::RhiTexture> iconAtlasTexture_;
        std::unique_ptr<rhi::RhiSampler> iconSampler_;
        rhi::RhiPipelineState* linePipelineState_ = nullptr;
        rhi::RhiPipelineState* iconPipelineState_ = nullptr;
        SizeT uploadedLineVertexCount_ = 0;
        SizeT uploadedIconVertexCount_ = 0;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
    };
} // namespace ve
