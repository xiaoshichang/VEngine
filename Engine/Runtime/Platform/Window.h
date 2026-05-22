#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"

#include <cstdint>
#include <string>

namespace ve
{
struct WindowExtent
{
    uint32_t width = 0;
    uint32_t height = 0;
};

struct WindowDesc
{
    std::string title = "VEngine";
    uint32_t width = 1280;
    uint32_t height = 720;
    bool visible = true;
};

class Window : public NonCopyable
{
public:
    virtual ~Window() = default;

    virtual void Show() = 0;
    virtual void Close() = 0;

    [[nodiscard]] virtual bool ShouldClose() const noexcept = 0;
    [[nodiscard]] virtual bool IsVisible() const noexcept = 0;
    [[nodiscard]] virtual bool IsFocused() const noexcept = 0;
    [[nodiscard]] virtual bool IsMinimized() const noexcept = 0;
    [[nodiscard]] virtual WindowExtent GetClientExtent() const noexcept = 0;
    [[nodiscard]] virtual const std::string& GetTitle() const noexcept = 0;
    [[nodiscard]] virtual void* GetNativeHandle() const noexcept = 0;
};
}
