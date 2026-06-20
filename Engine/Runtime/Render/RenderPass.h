#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Render/RenderFramePipelineData.h"

#include <memory>

namespace ve
{
    class BaseRenderer;
    class RTCamera;
    class RTScene;

    /// Data owned by one renderer instance across its lifetime.
    ///
    /// It stores renderer state and render-scene choices that are meaningful beyond a single pass. Frame-wide RHI data
    /// stays in FrameRenderPipelineData and pass-local attachment state stays in RenderPassData.
    struct RendererData
    {
        rhi::RhiColor clearColor{0.05f, 0.07f, 0.10f, 1.0f};
        std::shared_ptr<RTScene> scene;
        std::shared_ptr<RTCamera> camera;
        UInt32 activeRenderPassIndex = 0;
        bool active = false;
        bool renderPassOpen = false;
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

    /// Collects the RHI pass shape declared by one renderer pass for the current frame.
    class RenderPassBuilder : public NonCopyable
    {
    public:
        RenderPassBuilder() = default;

        void Reset(const char* passName, const FrameRenderPipelineData& frameData, const RendererData& rendererData) noexcept;

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

        [[nodiscard]] const rhi::RhiRenderPassDesc& GetRenderPassDesc() const noexcept;
        [[nodiscard]] const FrameRenderPipelineData& GetFrameData() const noexcept;
        [[nodiscard]] const RendererData& GetRendererData() const noexcept;
        [[nodiscard]] const rhi::RhiViewport& GetViewport() const noexcept;
        [[nodiscard]] const rhi::RhiScissorRect& GetScissor() const noexcept;

    private:
        const FrameRenderPipelineData* frameData_ = nullptr;
        const RendererData* rendererData_ = nullptr;
        rhi::RhiRenderPassDesc renderPassDesc_ = {};
        rhi::RhiViewport viewport_ = {};
        rhi::RhiScissorRect scissorRect_ = {};
    };

    /// Command-recording context passed to a renderer pass after its RHI pass has begun.
    class RenderPassContext : public NonCopyable
    {
    public:
        RenderPassContext(const FrameRenderPipelineData& frameData,
                          BaseRenderer& renderer,
                          const RenderPassData& passData) noexcept;

        [[nodiscard]] const FrameRenderPipelineData& GetFrameData() const noexcept;
        [[nodiscard]] BaseRenderer& GetRenderer() noexcept;
        [[nodiscard]] rhi::RhiDevice& GetDevice() noexcept;
        [[nodiscard]] rhi::RhiCommandList& GetCommandList() noexcept;
        [[nodiscard]] const RendererData& GetRendererData() const noexcept;
        [[nodiscard]] const RenderPassData& GetPassData() const noexcept;
        [[nodiscard]] const rhi::RhiRenderPassDesc& GetRenderPassDesc() const noexcept;
        [[nodiscard]] const rhi::RhiViewport& GetViewport() const noexcept;
        [[nodiscard]] const rhi::RhiScissorRect& GetScissor() const noexcept;

    private:
        const FrameRenderPipelineData* frameData_ = nullptr;
        BaseRenderer* renderer_ = nullptr;
        const RenderPassData* passData_ = nullptr;
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
