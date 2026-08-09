#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/FrameContext.h"
#include "Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h"

#include <array>
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

    class EditorGizmoRenderResources;

    struct EditorGizmoRenderPassInitParam
    {
        std::shared_ptr<const EditorGizmoDrawList> drawList;
        std::shared_ptr<EditorGizmoRenderResources> resources;
    };

    /// Render-thread-owned resources shared by the frame-local gizmo passes.
    ///
    /// Vertex buffers are split by FrameContext so each slot is updated only after its previous GPU submission has
    /// completed. Immutable icon resources and cached pipeline handles remain valid across frames.
    class EditorGizmoRenderResources final
    {
    private:
        friend class EditorGizmoRenderPass;

        std::array<std::unique_ptr<rhi::RhiBuffer>, RenderFrameContextCount> lineVertexBuffers_;
        std::array<std::unique_ptr<rhi::RhiBuffer>, RenderFrameContextCount> iconVertexBuffers_;
        std::array<UInt64, RenderFrameContextCount> lineVertexBufferCapacities_{};
        std::array<UInt64, RenderFrameContextCount> iconVertexBufferCapacities_{};
        std::unique_ptr<rhi::RhiTexture> iconAtlasTexture_;
        std::unique_ptr<rhi::RhiSampler> iconSampler_;
        rhi::RhiGraphicsPipelineState* linePipelineState_ = nullptr;
        rhi::RhiGraphicsPipelineState* iconPipelineState_ = nullptr;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
    };

    class EditorGizmoRenderPass final : public ViewRenderPass
    {
    public:
        explicit EditorGizmoRenderPass(EditorGizmoRenderPassInitParam initParam);

        void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) override;

    private:
        void Execute(RenderPassContext& context, UInt32 viewIndex);
        void EnsurePipeline(RenderPassContext& context);
        void EnsureIconResources(RenderPassContext& context);
        void UploadFrameResources(RenderPassContext& context);
        EditorGizmoRenderPassInitParam initParam_;
        rhi::RhiBuffer* lineVertexBuffer_ = nullptr;
        rhi::RhiBuffer* iconVertexBuffer_ = nullptr;
        SizeT uploadedLineVertexCount_ = 0;
        SizeT uploadedIconVertexCount_ = 0;
    };
} // namespace ve
