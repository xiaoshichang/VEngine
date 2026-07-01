#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Platform/Window.h"

#include <deque>
#include <memory>
#include <string>

namespace ve
{
    class MacWindow final : public Window
    {
    public:
        ~MacWindow() override;

        [[nodiscard]] static Result<std::unique_ptr<MacWindow>> Create(const WindowDesc& desc);
        [[nodiscard]] static Result<std::unique_ptr<Window>> CreatePlatformWindow(const WindowDesc& desc);

        void Show() override;
        void Close() override;
        [[nodiscard]] WindowPumpStatus PumpEvents() override;
        [[nodiscard]] bool TryPopOSEvent(OSEvent& outEvent) override;
        void SetCommandHandler(WindowCommandHandler handler) override;
        void PumpCommands() override;

        [[nodiscard]] bool ShouldClose() const noexcept override;
        [[nodiscard]] bool IsVisible() const noexcept override;
        [[nodiscard]] bool IsFocused() const noexcept override;
        [[nodiscard]] bool IsMinimized() const noexcept override;
        [[nodiscard]] WindowExtent GetClientExtent() const noexcept override;
        [[nodiscard]] const std::string& GetTitle() const noexcept override;
        [[nodiscard]] void* GetNativeHandle() const noexcept override;
        [[nodiscard]] void* GetNativeLayer() const noexcept override;

        void RequestClose();
        void OnNativeWindowWillClose();
        void OnNativeWindowFocusChanged(bool focused);
        void OnNativeWindowMiniaturized(bool minimized);
        void OnNativeWindowResized();
        void OnNativeMouseMoved(Int32 x, Int32 y);
        void OnNativeMouseButton(OSEventType type, InputMouseButton button, Int32 x, Int32 y);
        void OnNativeMouseWheel(Float32 deltaX, Float32 deltaY, Int32 x, Int32 y);
        void OnNativeKey(OSEventType type, UInt32 keyCode, UInt32 scanCode, InputModifierFlags modifiers, bool repeat);
        void OnNativeText(UInt32 codepoint);

    private:
        MacWindow() = default;

        [[nodiscard]] ErrorCode Initialize(const WindowDesc& desc);
        void QueueOSEvent(OSEvent event);

    private:
        void* window_ = nullptr;
        void* view_ = nullptr;
        void* metalLayer_ = nullptr;
        void* delegate_ = nullptr;
        std::string title_;
        WindowExtent clientExtent_ = {};
        std::deque<OSEvent> pendingOSEvents_;
        UInt32 pressedMouseButtonCount_ = 0;
        bool visible_ = false;
        bool focused_ = false;
        bool minimized_ = false;
        bool shouldClose_ = false;
    };
} // namespace ve
