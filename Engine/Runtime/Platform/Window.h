#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    enum class WindowPumpResult
    {
        Continue,
        Quit,
    };

    struct WindowPumpStatus
    {
        WindowPumpResult result = WindowPumpResult::Continue;
        int exitCode = 0;
    };

    struct WindowExtent
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    struct WindowMenuItemDesc
    {
        std::string menu;
        std::string label;
        std::string command;
    };

    struct WindowDesc
    {
        std::string title = "VEngine";
        uint32_t width = 1280;
        uint32_t height = 720;
        bool visible = true;
        std::vector<WindowMenuItemDesc> menuItems;
    };

    using WindowCommandHandler = std::function<void(std::string_view command)>;

    class Window : public NonCopyable
    {
    public:
        virtual ~Window() = default;

        [[nodiscard]] static Result<std::unique_ptr<Window>> Create(const WindowDesc& desc);

        virtual void Show() = 0;
        virtual void Close() = 0;
        [[nodiscard]] virtual WindowPumpStatus PumpEvents() = 0;
        virtual void SetCommandHandler(WindowCommandHandler handler) = 0;
        virtual void PumpCommands() = 0;

        [[nodiscard]] virtual bool ShouldClose() const noexcept = 0;
        [[nodiscard]] virtual bool IsVisible() const noexcept = 0;
        [[nodiscard]] virtual bool IsFocused() const noexcept = 0;
        [[nodiscard]] virtual bool IsMinimized() const noexcept = 0;
        [[nodiscard]] virtual WindowExtent GetClientExtent() const noexcept = 0;
        [[nodiscard]] virtual const std::string& GetTitle() const noexcept = 0;
        [[nodiscard]] virtual void* GetNativeHandle() const noexcept = 0;
        [[nodiscard]] virtual void* GetNativeLayer() const noexcept = 0;
    };
} // namespace ve
