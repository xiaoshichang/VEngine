#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Platform/Window.h"
#include "Engine/Runtime/Platform/Windows/Win32MessageLoop.h"

#include <deque>
#include <memory>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

#ifdef GetMessage
#undef GetMessage
#endif

namespace ve
{
    class Win32Window final : public Window
    {
    public:
        ~Win32Window() override;

        [[nodiscard]] static Result<std::unique_ptr<Win32Window>> Create(const WindowDesc& desc);
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
        [[nodiscard]] HWND GetWin32Handle() const noexcept;

    private:
        Win32Window() = default;

        [[nodiscard]] ErrorCode Initialize(const WindowDesc& desc);

        static LRESULT CALLBACK WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam);

        void UpdateClientExtent() noexcept;
        void QueueOSEvent(OSEvent event);

    private:
        HWND windowHandle_ = nullptr;
        std::string title_;
        WindowExtent clientExtent_ = {};
        Win32MessageLoop messageLoop_;
        std::deque<OSEvent> pendingOSEvents_;
        bool visible_ = false;
        bool focused_ = false;
        bool minimized_ = false;
        bool shouldClose_ = false;
    };
} // namespace ve
