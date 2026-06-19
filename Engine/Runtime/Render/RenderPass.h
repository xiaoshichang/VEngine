#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"

#include <memory>

namespace ve
{
    class RTCamera;
    class RTScene;
    class ShaderManager;

    /// Data owned by one Renderer invocation.
    ///
    /// Its lifetime is one scene-tree render inside a frame pipeline. Frame-wide RHI state is seeded by
    /// FrameRenderPipelineData; the renderer fills scene, camera, target surface, and pass-progress fields before
    /// executing its RenderPass list.
    struct RendererData
    {
        UInt64 frameIndex = 0;
        rhi::RhiExtent2D mainSurfaceExtent = {};
        rhi::RhiFormat mainColorFormat = rhi::RhiFormat::Bgra8Unorm;
        rhi::RhiColor clearColor{0.05f, 0.07f, 0.10f, 1.0f};
        std::shared_ptr<RTScene> scene;
        std::shared_ptr<RTCamera> camera;
        rhi::RhiDevice* device = nullptr;
        rhi::RhiCommandList* commandList = nullptr;
        rhi::RhiSwapchain* mainSwapchain = nullptr;
        ShaderManager* shaderManager = nullptr;
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

        void Reset(const char* passName, const RendererData& rendererData) noexcept;

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
        [[nodiscard]] const RendererData& GetRendererData() const noexcept;
        [[nodiscard]] const rhi::RhiViewport& GetViewport() const noexcept;
        [[nodiscard]] const rhi::RhiScissorRect& GetScissor() const noexcept;

    private:
        const RendererData* rendererData_ = nullptr;
        rhi::RhiRenderPassDesc renderPassDesc_ = {};
        rhi::RhiViewport viewport_ = {};
        rhi::RhiScissorRect scissorRect_ = {};
    };

    /// Command-recording context passed to a renderer pass after its RHI pass has begun.
    class RenderPassContext : public NonCopyable
    {
    public:
        RenderPassContext(rhi::RhiDevice& device,
                          rhi::RhiCommandList& commandList,
                          const RendererData& rendererData,
                          const rhi::RhiRenderPassDesc& renderPassDesc,
                          const rhi::RhiViewport& viewport,
                          const rhi::RhiScissorRect& scissorRect) noexcept;

        [[nodiscard]] rhi::RhiDevice& GetDevice() noexcept;
        [[nodiscard]] rhi::RhiCommandList& GetCommandList() noexcept;
        [[nodiscard]] const RendererData& GetRendererData() const noexcept;
        [[nodiscard]] const rhi::RhiRenderPassDesc& GetRenderPassDesc() const noexcept;
        [[nodiscard]] const rhi::RhiViewport& GetViewport() const noexcept;
        [[nodiscard]] const rhi::RhiScissorRect& GetScissor() const noexcept;

    private:
        rhi::RhiDevice* device_ = nullptr;
        rhi::RhiCommandList* commandList_ = nullptr;
        const RendererData* rendererData_ = nullptr;
        const rhi::RhiRenderPassDesc* renderPassDesc_ = nullptr;
        rhi::RhiViewport viewport_ = {};
        rhi::RhiScissorRect scissorRect_ = {};
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
