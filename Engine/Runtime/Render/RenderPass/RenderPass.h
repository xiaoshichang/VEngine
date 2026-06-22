#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"

#include <memory>

namespace ve
{
    class RTCamera;
    class RTScene;

    /// Data owned by one renderer instance across its lifetime.
    ///
    /// It stores render-scene choices that are meaningful beyond a single pass. Frame-wide RHI data stays in
    /// FrameRenderPipelineData and pass-local attachment state stays in RenderPassData.
    struct RendererData
    {
        std::shared_ptr<RTScene> scene;
        std::shared_ptr<RTCamera> resolvedCamera;
    };

    /// Data owned by one RenderPass invocation.
    ///
    /// Its lifetime is one pass setup and execution. It deliberately contains only pass-local RHI state; the long-lived
    /// RenderPass implementation is stored separately by the Renderer.
    struct RenderPassData
    {
        rhi::RhiRenderPassDesc renderPassDesc = {};
        rhi::RhiViewport viewport = {};
        rhi::RhiScissorRect scissorRect = {};
    };

    struct RenderPassBuilderInitParam
    {
        const char* passName = "";
        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;
    };

    /// Collects the RHI pass shape declared by one renderer pass for the current frame.
    class RenderPassBuilder : public NonCopyable
    {
    public:
        explicit RenderPassBuilder(RenderPassBuilderInitParam initParam) noexcept;

        void SetRenderArea(const rhi::RhiRenderArea& renderArea) noexcept;
        void SetViewport(const rhi::RhiViewport& viewport) noexcept;
        void SetScissor(const rhi::RhiScissorRect& scissorRect) noexcept;

        void AddSwapchainColorAttachment(rhi::RhiLoadAction loadAction, rhi::RhiStoreAction storeAction, rhi::RhiColor clearColor) noexcept;

        void
        AddTextureColorAttachment(rhi::RhiTexture& texture, rhi::RhiLoadAction loadAction, rhi::RhiStoreAction storeAction, rhi::RhiColor clearColor) noexcept;

        void SetDepthStencilAttachment(rhi::RhiTexture& texture,
                                       rhi::RhiLoadAction depthLoadAction,
                                       rhi::RhiStoreAction depthStoreAction,
                                       rhi::RhiDepthStencilClearValue clearValue) noexcept;

        [[nodiscard]] RenderPassData Build() const noexcept;

        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;

    private:
        rhi::RhiRenderPassDesc renderPassDesc_ = {};
        rhi::RhiViewport viewport_ = {};
        rhi::RhiScissorRect scissorRect_ = {};
    };

    struct RenderPassContextInitParam
    {
        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;
        const RenderPassData& passData;
    };

    /// Command-recording context passed to a renderer pass after its RHI pass has begun.
    class RenderPassContext : public NonCopyable
    {
    public:
        explicit RenderPassContext(RenderPassContextInitParam initParam) noexcept;

        const FrameRenderPipelineData& frameData;
        const RendererData& rendererData;
        const RenderPassData& passData;
        rhi::RhiDevice& device;
        rhi::RhiCommandList& commandList;
    };

    /// Long-lived renderer pass implementation. Per-frame attachment state is declared through RenderPassBuilder.
    class RenderPass : public NonCopyable
    {
    public:
        RenderPass() = default;
        virtual ~RenderPass() = default;

        [[nodiscard]] virtual const char* GetName() const noexcept = 0;
        virtual void Setup(RenderPassBuilder& builder) = 0;
        virtual void Execute(RenderPassContext& context) = 0;
    };
} // namespace ve
