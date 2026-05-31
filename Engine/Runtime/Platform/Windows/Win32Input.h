#pragma once

#include "Engine/Runtime/Core/Types.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>

namespace ve
{
    class InputSystem;
    class Window;

    void BeginWin32WindowInputFrame(Window& window, InputSystem& inputSystem) noexcept;
    [[nodiscard]] bool HandleWin32InputMessage(InputSystem& inputSystem,
                                               HWND window,
                                               UINT message,
                                               WPARAM wParam,
                                               LPARAM lParam,
                                               LRESULT& result) noexcept;
} // namespace ve
