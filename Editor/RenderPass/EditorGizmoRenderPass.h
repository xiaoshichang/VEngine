#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"
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

    struct EditorGizmoDrawList
    {
        std::vector<EditorGizmoVertex> vertices;
    };

    struct EditorGizmoRenderPassDesc
    {
        std::shared_ptr<RTRenderTexture> colorTexture;
        std::shared_ptr<const EditorGizmoDrawList> drawList;
    };

    class EditorGizmoRenderPass final : public RenderPass
    {
    public:
        explicit EditorGizmoRenderPass(EditorGizmoRenderPassDesc desc);

        [[nodiscard]] const char* GetName() const noexcept override;
        void Setup(RenderPassBuilder& builder) override;
        void Execute(RenderPassContext& context) override;

    private:
        void EnsurePipeline(RenderPassContext& context);
        void UploadFrameResources(RenderPassContext& context);
        [[nodiscard]] rhi::RhiFormat ResolveTargetFormat(const RenderPassContext& context) const noexcept;

        EditorGizmoRenderPassDesc desc_;
        std::unique_ptr<rhi::RhiBuffer> vertexBuffer_;
        std::unique_ptr<rhi::RhiBuffer> uniformBuffer_;
        std::unique_ptr<rhi::RhiPipelineState> pipelineState_;
        SizeT uploadedVertexCount_ = 0;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
    };
} // namespace ve
