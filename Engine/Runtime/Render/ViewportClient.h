#pragma once

#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Render/RenderTarget.h"

#include <string>

namespace ve
{
    /// Describes which kind of output is currently bound to a viewport.
    ///
    /// A viewport can present to a native window surface or render into an off-screen render target, but not both at
    /// the same time.
    enum class ViewportBindingKind
    {
        None,
        WindowSurface,
        RenderTarget,
    };

    /// Owns the runtime-facing state for one logical viewport.
    ///
    /// ViewportClient keeps the output binding separate from camera state. The caller chooses whether this viewport is
    /// backed by a native window surface or by an off-screen render target, then later merges camera and viewport
    /// information when building render work for the Render Thread.
    class ViewportClient
    {
    public:
        explicit ViewportClient(std::string name = "ViewportClient");

        [[nodiscard]] const std::string& GetName() const noexcept;
        /// Updates the viewport label used by tools and diagnostics.
        void SetName(std::string name);

        /// Returns the current binding kind.
        [[nodiscard]] ViewportBindingKind GetBindingKind() const noexcept;

        /// Returns true when the viewport is currently bound to any output target.
        [[nodiscard]] bool IsBound() const noexcept;

        /// Returns true when the viewport is bound to a native window surface.
        [[nodiscard]] bool IsWindowSurfaceBound() const noexcept;

        /// Returns true when the viewport is bound to an off-screen render target.
        [[nodiscard]] bool IsRenderTargetBound() const noexcept;

        /// Returns the current render target description.
        [[nodiscard]] RenderTarget& GetRenderTarget() noexcept;
        [[nodiscard]] const RenderTarget& GetRenderTarget() const noexcept;

        /// Binds the viewport to a native window surface.
        ///
        /// This clears any previous texture-backed binding. nativeWindow must describe the platform window handle,
        /// and nativeLayer should carry the presentation layer when the platform exposes one.
        void BindWindowSurface(void* nativeWindow, void* nativeLayer, WindowExtent extent);

        /// Binds the viewport to an off-screen render target.
        ///
        /// This clears any previous native surface binding. The supplied render target should already describe the
        /// texture-backed output the caller wants to render into.
        void BindRenderTarget(RenderTarget renderTarget);

        /// Clears the current binding and returns the viewport to an unbound state.
        void ResetBinding() noexcept;

        /// Returns the native window handle for a window-backed viewport, or nullptr when unbound / off-screen.
        [[nodiscard]] void* GetNativeWindow() const noexcept;
        /// Returns the native presentation layer for a window-backed viewport, or nullptr when unbound / off-screen.
        [[nodiscard]] void* GetNativeLayer() const noexcept;

        /// Returns the extent carried by the current render target.
        [[nodiscard]] WindowExtent GetExtent() const noexcept;

        /// Updates the viewport from a native window.
        ///
        /// This is a convenience wrapper around BindWindowSurface() for the common Player and editor window cases.
        void SyncFromWindow(const Window& window);

        /// Updates the extent of an existing window-backed viewport.
        ///
        /// Main Thread owns platform window message pumping, but Scene Thread owns render-facing viewport state after
        /// startup. Resize events should therefore carry the new extent to Scene Thread and use this method instead of
        /// rebinding native window handles every frame.
        void ResizeWindowSurface(WindowExtent extent);

    private:
        std::string name_;
        RenderTarget renderTarget_;
        ViewportBindingKind bindingKind_ = ViewportBindingKind::None;
        void* nativeWindow_ = nullptr;
        void* nativeLayer_ = nullptr;
    };
} // namespace ve
