#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h"

#include <memory>
#include <string>
#include <vector>

namespace ve
{
    class FrameGraph;
    class RTCamera;
    class RTRenderItem;
    class RTScene;
    class RTShaderResource;

    /// Renderer-owned scene choices and the queue lists built once for one view.
    struct RendererData
    {
        std::shared_ptr<RTScene> scene;
        std::shared_ptr<RTCamera> resolvedCamera;
        std::vector<std::shared_ptr<RTRenderItem>> opaqueItems;
        std::vector<std::shared_ptr<RTRenderItem>> transparentItems;
    };

    /// The current logical attachment versions shared while renderer passes register themselves.
    struct RendererFrameGraphData
    {
        FrameGraphTextureHandle color;
        FrameGraphTextureHandle depth;
    };

    struct RenderPassData
    {
        rhi::RhiRenderPassDesc renderPassDesc = {};
        rhi::RhiViewport viewport = {};
        rhi::RhiScissorRect scissorRect = {};
    };

    struct RenderPassContextInitParam
    {
        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;
        const RenderPassData& passData;
    };

    /// Draw-time facade supplied after the frame graph has resolved and begun the native render pass.
    class RenderPassContext final : public NonCopyable
    {
    public:
        explicit RenderPassContext(RenderPassContextInitParam initParam) noexcept;

        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;
        const RenderPassData& passData;
        rhi::RhiDevice& device;
        rhi::RhiCommandList& commandList;
    };

    /// Long-lived renderer pass that declares one or more nodes in a frame graph.
    class RenderPass : public NonCopyable
    {
    public:
        RenderPass() = default;
        virtual ~RenderPass() = default;

        virtual void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) = 0;
    };

    struct SceneRenderPassSettings
    {
        std::string passName;
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
        bool transparent = false;
    };

    /// Shared static-mesh draw implementation used by opaque and transparent graph passes.
    class SceneRenderPassExecutor final : public NonCopyable
    {
    public:
        explicit SceneRenderPassExecutor(SceneRenderPassSettings settings);

        [[nodiscard]] ErrorCode Draw(RenderPassContext& context, const std::vector<std::shared_ptr<RTRenderItem>>& items);

    private:
        [[nodiscard]] ErrorCode EnsurePipeline(RenderPassContext& context, const std::vector<std::shared_ptr<RTRenderItem>>& items);
        [[nodiscard]] bool BindMaterialUniform(RenderPassContext& context, const RTRenderItem& item);
        [[nodiscard]] rhi::RhiFormat ResolveTargetFormat(const RenderPassContext& context) const noexcept;

        SceneRenderPassSettings settings_;
        rhi::RhiPipelineState* pipelineState_ = nullptr;
        rhi::RhiFormat pipelineColorFormat_ = rhi::RhiFormat::Unknown;
        rhi::RhiFillMode pipelineFillMode_ = rhi::RhiFillMode::Solid;
        std::weak_ptr<RTShaderResource> pipelineShaderResource_;
        bool pipelineDepthEnabled_ = false;
    };
} // namespace ve
