#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Platform/Window.h"

#include <deque>
#include <memory>
#include <string>

namespace ve
{
    class IOSWindow final : public Window
    {
    public:
        ~IOSWindow() override;

        static void RegisterMainView(void* view) noexcept;
        static void ClearRegisteredMainView() noexcept;
        static void NotifyRegisteredMainViewResized();

        [[nodiscard]] static Result<std::unique_ptr<IOSWindow>> Create(const WindowDesc& desc);
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

        void OnNativeViewResized();

    private:
        IOSWindow() = default;

        [[nodiscard]] ErrorCode Initialize(const WindowDesc& desc);
        void QueueOSEvent(OSEvent event);

    private:
        void* view_ = nullptr;
        void* metalLayer_ = nullptr;
        std::string title_;
        WindowExtent clientExtent_ = {};
        std::deque<OSEvent> pendingOSEvents_;
        bool visible_ = false;
        bool focused_ = false;
        bool minimized_ = false;
        bool shouldClose_ = false;
    };
} // namespace ve
