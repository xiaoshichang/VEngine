#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Render/RenderTarget.h"
#include "Engine/Runtime/Render/ViewportClient.h"

#include <iostream>

namespace
{
    class FakeWindow final : public ve::Window
    {
    public:
        void Show() override {}
        void Close() override {}
        [[nodiscard]] ve::WindowPumpStatus PumpEvents() override
        {
            return {};
        }
        [[nodiscard]] bool TryPopOSEvent(ve::OSEvent& outEvent) override
        {
            (void)outEvent;
            return false;
        }
        void SetCommandHandler(ve::WindowCommandHandler handler) override
        {
            (void)handler;
        }
        void PumpCommands() override {}
        [[nodiscard]] bool ShouldClose() const noexcept override
        {
            return false;
        }
        [[nodiscard]] bool IsVisible() const noexcept override
        {
            return true;
        }
        [[nodiscard]] bool IsFocused() const noexcept override
        {
            return true;
        }
        [[nodiscard]] bool IsMinimized() const noexcept override
        {
            return false;
        }
        [[nodiscard]] ve::WindowExtent GetClientExtent() const noexcept override
        {
            return clientExtent;
        }
        [[nodiscard]] const std::string& GetTitle() const noexcept override
        {
            return title;
        }
        [[nodiscard]] void* GetNativeHandle() const noexcept override
        {
            return nativeWindow;
        }
        [[nodiscard]] void* GetNativeLayer() const noexcept override
        {
            return nativeLayer;
        }

        std::string title = "FakeWindow";
        ve::WindowExtent clientExtent{1920, 1080};
        void* nativeWindow = reinterpret_cast<void*>(0x1234);
        void* nativeLayer = reinterpret_cast<void*>(0x5678);
    };

    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool TestRenderTarget()
    {
        bool passed = true;

        ve::RenderTarget target;
        passed &= Expect(!target.IsValid(), "Default render target should be invalid");

        target.SetName("PlayerViewport");
        target.SetKind(ve::RenderTargetKind::Texture);
        target.SetExtent(ve::WindowExtent{1280, 720});
        target.SetColorFormat(ve::rhi::RhiFormat::Bgra8Unorm);

        passed &= Expect(target.IsValid(), "Render target with extent should be valid");
        passed &= Expect(target.GetName() == "PlayerViewport", "Render target name should persist");
        passed &= Expect(target.GetKind() == ve::RenderTargetKind::Texture, "Render target kind should persist");
        passed &= Expect(target.GetExtent().width == 1280 && target.GetExtent().height == 720, "Render target extent should persist");

        return passed;
    }

    bool TestViewportClientSync()
    {
        bool passed = true;

        FakeWindow window;
        ve::ViewportClient viewportClient("PlayerViewport");

        viewportClient.SyncFromWindow(window);

        passed &= Expect(viewportClient.IsBound(), "ViewportClient should expose a binding");
        passed &= Expect(viewportClient.IsWindowSurfaceBound(), "ViewportClient should bind a window surface");
        passed &= Expect(viewportClient.GetRenderTarget().GetKind() == ve::RenderTargetKind::Window, "ViewportClient should bind a window render target");
        passed &= Expect(viewportClient.GetRenderTarget().GetExtent().width == 1920 && viewportClient.GetRenderTarget().GetExtent().height == 1080,
                         "ViewportClient should sync extent from window");
        passed &= Expect(viewportClient.GetNativeWindow() == window.nativeWindow, "ViewportClient should preserve native window handle");
        passed &= Expect(viewportClient.GetNativeLayer() == window.nativeLayer, "ViewportClient should preserve native layer handle");
        passed &= Expect(viewportClient.GetBindingKind() == ve::ViewportBindingKind::WindowSurface, "ViewportClient should report window-surface binding kind");

        viewportClient.ResizeWindowSurface(ve::WindowExtent{1600, 900});
        passed &= Expect(viewportClient.IsWindowSurfaceBound(), "Viewport resize should preserve window binding");
        passed &= Expect(viewportClient.GetRenderTarget().GetExtent().width == 1600 && viewportClient.GetRenderTarget().GetExtent().height == 900,
                         "Viewport resize should update the window render target extent");
        passed &= Expect(viewportClient.GetNativeWindow() == window.nativeWindow, "Viewport resize should preserve native window handle");
        passed &= Expect(viewportClient.GetNativeLayer() == window.nativeLayer, "Viewport resize should preserve native layer handle");

        ve::RenderTarget offscreenTarget;
        offscreenTarget.SetName("SceneView");
        offscreenTarget.SetKind(ve::RenderTargetKind::Texture);
        offscreenTarget.SetExtent(ve::WindowExtent{1024, 768});
        viewportClient.BindRenderTarget(std::move(offscreenTarget));

        passed &= Expect(viewportClient.IsRenderTargetBound(), "ViewportClient should bind an off-screen target");
        passed &= Expect(viewportClient.GetNativeWindow() == nullptr, "Off-screen viewport should clear native window");
        passed &= Expect(viewportClient.GetNativeLayer() == nullptr, "Off-screen viewport should clear native layer");
        passed &=
            Expect(viewportClient.GetRenderTarget().GetKind() == ve::RenderTargetKind::Texture, "Off-screen viewport should normalize the render target kind");
        passed &= Expect(viewportClient.GetBindingKind() == ve::ViewportBindingKind::RenderTarget, "ViewportClient should report off-screen binding kind");

        viewportClient.ResetBinding();
        passed &= Expect(!viewportClient.IsBound(), "ViewportClient reset should clear the binding");
        passed &= Expect(viewportClient.GetBindingKind() == ve::ViewportBindingKind::None, "ViewportClient reset should return to an unbound state");

        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestRenderTarget();
    passed &= TestViewportClientSync();

    if (passed)
    {
        std::cout << "VEngineViewportTests passed" << '\n';
        return 0;
    }

    return 1;
}
