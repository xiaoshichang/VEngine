#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Render/RenderResourceLifetime.h"
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
        bool createDepthTexture = true;
    };

    /// Scene Thread object for a texture that can be both rendered into and sampled from.
    ///
    /// RenderTexture binds together the CPU-side render target description, the Render Thread texture proxy, and the
    /// sampled-view handle used by editor UI or future material binding. RenderTarget stays a lightweight output
    /// description; RenderTexture owns the actual texture-backed render resource.
    class RenderTexture final : public NonCopyable
    {
    public:
        RenderTexture();
        explicit RenderTexture(RenderTextureDesc desc);
        ~RenderTexture();

        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] const std::string& GetName() const noexcept;
        [[nodiscard]] WindowExtent GetExtent() const noexcept;
        [[nodiscard]] rhi::RhiFormat GetColorFormat() const noexcept;
        [[nodiscard]] const RenderTarget& GetRenderTarget() const noexcept;

        void InitRenderResource(RenderSystem& renderSystem);

        [[nodiscard]] std::shared_ptr<RTRenderTexture> GetRTRenderTexture() const noexcept;
        [[nodiscard]] void* GetRenderResourceViewHandle() const noexcept;

    private:
        void EnsureRenderThreadProxy();
        [[nodiscard]] RenderTextureDesc BuildDesc() const;

        RenderTextureDesc desc_;
        RenderTarget renderTarget_;
        std::shared_ptr<RTRenderTexture> rtRenderTexture_;
        RenderSystem* renderSystem_ = nullptr;
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
        [[nodiscard]] std::shared_ptr<rhi::RhiTexture> GetTextureShared() const noexcept;
        [[nodiscard]] rhi::RhiTexture* GetDepthTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetDepthTexture() const noexcept;
        [[nodiscard]] std::shared_ptr<rhi::RhiTexture> GetDepthTextureShared() const noexcept;
        [[nodiscard]] void* GetRenderResourceViewHandle() const noexcept;
        [[nodiscard]] bool MatchesDesc(const RenderTextureDesc& desc) const noexcept;
        [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

        void InitRenderResource(rhi::RhiDevice& device, RenderTextureDesc desc);

    private:
        RenderTextureDesc desc_;
        std::shared_ptr<rhi::RhiTexture> texture_;
        std::shared_ptr<rhi::RhiTexture> depthTexture_;
        std::atomic<void*> nativeSampledViewHandle_{nullptr};
    };
} // namespace ve
