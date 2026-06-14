#pragma once

#include "Engine/RHI/Common/RhiDevice.h"
#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Platform/Window.h"

#include <atomic>
#include <memory>
#include <string>

namespace ve
{
    class RenderSystem;
    class RTRenderTarget;

    namespace rhi
    {
        class RhiDevice;
    }

    enum class RenderTargetKind
    {
        Texture,
        Window,
    };

    struct RenderTargetDesc
    {
        std::string name = "RenderTarget";
        RenderTargetKind kind = RenderTargetKind::Texture;
        WindowExtent extent = {};
        rhi::RhiFormat colorFormat = rhi::RhiFormat::Bgra8Unorm;
    };

    class RenderTarget
    {
    public:
        RenderTarget();
        explicit RenderTarget(RenderTargetDesc desc);

        [[nodiscard]] bool IsValid() const noexcept;

        [[nodiscard]] const std::string& GetName() const noexcept;
        void SetName(std::string name);

        [[nodiscard]] RenderTargetKind GetKind() const noexcept;
        void SetKind(RenderTargetKind kind) noexcept;

        [[nodiscard]] WindowExtent GetExtent() const noexcept;
        void SetExtent(WindowExtent extent) noexcept;

        [[nodiscard]] rhi::RhiFormat GetColorFormat() const noexcept;
        void SetColorFormat(rhi::RhiFormat colorFormat) noexcept;

        /// Ensures the Render Thread proxy exists and owns a matching RHI resource.
        ///
        /// The Scene Thread owns RenderTarget and may change its CPU-side description. RHI access stays behind this
        /// explicit handoff: InitRenderResource captures the shared RT proxy into the render command queue so the
        /// Render Thread can create or refresh the backend texture at a safe point.
        [[nodiscard]] ErrorCode InitRenderResource(RenderSystem& renderSystem);

        [[nodiscard]] std::shared_ptr<RTRenderTarget> GetRTRenderTarget() const noexcept;

        void Reset() noexcept;

    private:
        void EnsureRenderThreadProxy();

        std::string name_ = "RenderTarget";
        RenderTargetKind kind_ = RenderTargetKind::Texture;
        WindowExtent extent_ = {};
        rhi::RhiFormat colorFormat_ = rhi::RhiFormat::Bgra8Unorm;
        std::shared_ptr<RTRenderTarget> rtRenderTarget_;
    };

    /// Render Thread proxy for a RenderTarget.
    ///
    /// The RT prefix marks ownership and mutation on the Render Thread. RenderTarget keeps this proxy alive through a
    /// shared_ptr so a queued frame can finish touching the RHI texture even if the Scene Thread destroys or replaces
    /// the CPU-side RenderTarget object first.
    class RTRenderTarget final
    {
    public:
        explicit RTRenderTarget(RenderTargetDesc desc);

        [[nodiscard]] const RenderTargetDesc& GetDesc() const noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] rhi::RhiTexture* GetTexture() noexcept;
        [[nodiscard]] const rhi::RhiTexture* GetTexture() const noexcept;
        [[nodiscard]] void* GetNativeSampledViewHandle() const noexcept;

        [[nodiscard]] ErrorCode InitRenderResource(rhi::RhiDevice& device, RenderTargetDesc desc);
        void ResetRenderResource() noexcept;

    private:
        RenderTargetDesc desc_;
        std::unique_ptr<rhi::RhiTexture> texture_;
        std::atomic<void*> nativeSampledViewHandle_{nullptr};
    };
} // namespace ve
