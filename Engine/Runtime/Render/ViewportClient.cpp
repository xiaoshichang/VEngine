#include "Engine/Runtime/Render/ViewportClient.h"

#include "Engine/Runtime/Core/Assert.h"

#include <utility>

namespace ve
{
    ViewportClient::ViewportClient(std::string name)
        : name_(std::move(name))
    {
    }

    const std::string& ViewportClient::GetName() const noexcept
    {
        return name_;
    }

    void ViewportClient::SetName(std::string name)
    {
        name_ = std::move(name);
    }

    ViewportBindingKind ViewportClient::GetBindingKind() const noexcept
    {
        return bindingKind_;
    }

    bool ViewportClient::IsBound() const noexcept
    {
        return bindingKind_ != ViewportBindingKind::None;
    }

    bool ViewportClient::IsWindowSurfaceBound() const noexcept
    {
        return bindingKind_ == ViewportBindingKind::WindowSurface;
    }

    bool ViewportClient::IsRenderTargetBound() const noexcept
    {
        return bindingKind_ == ViewportBindingKind::RenderTarget;
    }

    RenderTarget& ViewportClient::GetRenderTarget() noexcept
    {
        return renderTarget_;
    }

    const RenderTarget& ViewportClient::GetRenderTarget() const noexcept
    {
        return renderTarget_;
    }

    void ViewportClient::BindWindowSurface(void* nativeWindow, void* nativeLayer, WindowExtent extent)
    {
        VE_ASSERT_MESSAGE(nativeWindow != nullptr || nativeLayer != nullptr,
                          "BindWindowSurface requires a native window or native layer handle.");
        VE_ASSERT_MESSAGE(extent.width != 0 && extent.height != 0, "BindWindowSurface requires a valid extent.");

        /// A window-backed viewport should not retain texture-specific binding state.
        renderTarget_.SetName(name_.empty() ? "Viewport" : name_);
        renderTarget_.SetKind(RenderTargetKind::Window);
        renderTarget_.SetExtent(extent);
        renderTarget_.SetColorFormat(rhi::RhiFormat::Bgra8Unorm);
        bindingKind_ = ViewportBindingKind::WindowSurface;
        nativeWindow_ = nativeWindow;
        nativeLayer_ = nativeLayer;
    }

    void ViewportClient::BindRenderTarget(RenderTarget renderTarget)
    {
        VE_ASSERT_MESSAGE(renderTarget.GetKind() == RenderTargetKind::Texture,
                          "BindRenderTarget requires a texture-backed render target.");
        VE_ASSERT_MESSAGE(renderTarget.GetExtent().width != 0 && renderTarget.GetExtent().height != 0,
                          "BindRenderTarget requires a valid extent.");

        /// An off-screen viewport should not retain a native window handle.
        /// The caller is expected to pass a texture-backed render target here.
        renderTarget_ = std::move(renderTarget);
        bindingKind_ = ViewportBindingKind::RenderTarget;
        nativeWindow_ = nullptr;
        nativeLayer_ = nullptr;
    }

    void ViewportClient::ResetBinding() noexcept
    {
        renderTarget_.Reset();
        bindingKind_ = ViewportBindingKind::None;
        nativeWindow_ = nullptr;
        nativeLayer_ = nullptr;
    }

    void* ViewportClient::GetNativeWindow() const noexcept
    {
        return nativeWindow_;
    }

    void* ViewportClient::GetNativeLayer() const noexcept
    {
        return nativeLayer_;
    }

    WindowExtent ViewportClient::GetExtent() const noexcept
    {
        return renderTarget_.GetExtent();
    }

    void ViewportClient::SyncFromWindow(const Window& window)
    {
        BindWindowSurface(window.GetNativeHandle(), window.GetNativeLayer(), window.GetClientExtent());
    }

    void ViewportClient::ResizeWindowSurface(WindowExtent extent)
    {
        VE_ASSERT_MESSAGE(bindingKind_ == ViewportBindingKind::WindowSurface,
                          "ResizeWindowSurface requires a window-backed viewport.");
        VE_ASSERT_MESSAGE(extent.width != 0 && extent.height != 0, "ResizeWindowSurface requires a valid extent.");

        renderTarget_.SetExtent(extent);
    }
} // namespace ve
