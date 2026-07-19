#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Render/RenderTarget.h"

#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace ve
{
    class RenderSystem;
    class RTRenderTexture;

    struct RenderTextureDesc
    {
        std::string name = "RenderTexture";
        WindowExtent extent = {};
        rhi::RhiFormat colorFormat = rhi::RhiFormat::Bgra8Unorm;
        rhi::RhiColor optimizedClearColor{0.05f, 0.07f, 0.10f, 1.0f};
    };

    /// Scene Thread object for a texture that can be both rendered into and sampled from.
    ///
    /// RenderTexture binds together the CPU-side render target description, the Render Thread texture proxy, and the
    /// sampled-view handle used by editor UI or future material binding. RenderTarget stays a lightweight output
    /// description; RenderTexture owns the actual texture-backed render resource.
    class RenderTexture
    {
    public:
        RenderTexture();
        explicit RenderTexture(RenderTextureDesc desc);

        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] const std::string& GetName() const noexcept;
        [[nodiscard]] WindowExtent GetExtent() const noexcept;
        [[nodiscard]] rhi::RhiFormat GetColorFormat() const noexcept;
        [[nodiscard]] const RenderTarget& GetRenderTarget() const noexcept;

        void Resize(WindowExtent extent);

        void InitRenderResource(RenderSystem& renderSystem);

        [[nodiscard]] std::shared_ptr<RTRenderTexture> GetRTRenderTexture() const noexcept;
        [[nodiscard]] void* GetRenderResourceViewHandle() const noexcept;

    private:
        void EnsureRenderThreadProxy();
        [[nodiscard]] RenderTextureDesc BuildDesc() const;

        RenderTextureDesc desc_;
        RenderTarget renderTarget_;
        std::shared_ptr<RTRenderTexture> rtRenderTexture_;
    };

    /// Render Thread proxy for a RenderTexture.
    ///
    /// The proxy owns the backend texture resource and exposes the sampled view handle after initialization. It is kept
    /// alive by shared_ptr captures so queued render and editor UI work can finish after the Scene Thread replaces a
    /// RenderTexture during resize.
    class RTRenderTexture final
    {
    public:
        explicit RTRenderTexture(RenderTextureDesc desc);

        [[nodiscard]] const RenderTextureDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] rhi::RhiTexture* GetTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetTexture() const noexcept;
        [[nodiscard]] rhi::RhiTexture* GetDepthTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetDepthTexture() const noexcept;
        [[nodiscard]] void* GetRenderResourceViewHandle() const noexcept;

        void InitRenderResource(rhi::RhiDevice& device,
                                RenderTextureDesc desc,
                                std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources);
        void ResetRenderResource(std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources) noexcept;

    private:
        RenderTextureDesc desc_;
        std::unique_ptr<rhi::RhiTexture> texture_;
        std::unique_ptr<rhi::RhiTexture> depthTexture_;
        std::atomic<void*> nativeSampledViewHandle_{nullptr};
    };
} // namespace ve
