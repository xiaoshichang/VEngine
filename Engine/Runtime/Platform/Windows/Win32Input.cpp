#include "Engine/Runtime/Platform/Windows/Win32Input.h"

#include "Engine/Runtime/Input/InputSystem.h"
#include "Engine/Runtime/Platform/Window.h"

#include <windowsx.h>

namespace ve
{
    namespace
    {
        [[nodiscard]] KeyCode MapVirtualKeyToKeyCode(WPARAM wParam, LPARAM lParam) noexcept
        {
            if (wParam >= 'A' && wParam <= 'Z')
            {
                return static_cast<KeyCode>(static_cast<UInt16>(KeyCode::A) + static_cast<UInt16>(wParam - 'A'));
            }

            if (wParam >= '0' && wParam <= '9')
            {
                return static_cast<KeyCode>(static_cast<UInt16>(KeyCode::Num0) + static_cast<UInt16>(wParam - '0'));
            }

            switch (wParam)
            {
            case VK_SPACE:
                return KeyCode::Space;
            case VK_RETURN:
                return KeyCode::Enter;
            case VK_ESCAPE:
                return KeyCode::Escape;
            case VK_TAB:
                return KeyCode::Tab;
            case VK_BACK:
                return KeyCode::Backspace;
            case VK_LEFT:
                return KeyCode::Left;
            case VK_RIGHT:
                return KeyCode::Right;
            case VK_UP:
                return KeyCode::Up;
            case VK_DOWN:
                return KeyCode::Down;
            case VK_SHIFT:
            {
                const UINT scanCode = static_cast<UINT>((lParam >> 16) & 0xFFu);
                const UINT virtualKey = MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX);
                return virtualKey == VK_RSHIFT ? KeyCode::RightShift : KeyCode::LeftShift;
            }
            case VK_CONTROL:
                return (lParam & (1ll << 24)) != 0 ? KeyCode::RightControl : KeyCode::LeftControl;
            case VK_MENU:
                return (lParam & (1ll << 24)) != 0 ? KeyCode::RightAlt : KeyCode::LeftAlt;
            default:
                return KeyCode::Unknown;
            }
        }

        [[nodiscard]] Float32 GetClientMouseX(LPARAM lParam) noexcept
        {
            return static_cast<Float32>(GET_X_LPARAM(lParam));
        }

        [[nodiscard]] Float32 GetClientMouseY(LPARAM lParam) noexcept
        {
            return static_cast<Float32>(GET_Y_LPARAM(lParam));
        }

        void SubmitClientMousePosition(InputSystem& inputSystem, LPARAM lParam) noexcept
        {
            inputSystem.SubmitMousePosition(GetClientMouseX(lParam), GetClientMouseY(lParam));
        }
    } // namespace

    void BeginWin32WindowInputFrame(Window& window, InputSystem& inputSystem) noexcept
    {
        inputSystem.BeginMainFrame();
        inputSystem.SetFocused(window.IsFocused());

        const WindowExtent extent = window.GetClientExtent();
        inputSystem.SetViewportSize(extent.width, extent.height);
    }

    bool HandleWin32InputMessage(InputSystem& inputSystem,
                                 HWND window,
                                 UINT message,
                                 WPARAM wParam,
                                 LPARAM lParam,
                                 LRESULT& result) noexcept
    {
        switch (message)
        {
        case WM_SETFOCUS:
            inputSystem.SetFocused(true);
            return false;
        case WM_KILLFOCUS:
            inputSystem.SetFocused(false);
            return false;
        case WM_SIZE:
            inputSystem.SetViewportSize(LOWORD(lParam), HIWORD(lParam));
            return false;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if ((lParam & (1ll << 30)) == 0)
            {
                inputSystem.SubmitKey(MapVirtualKeyToKeyCode(wParam, lParam), true);
            }
            return false;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            if (wParam == VK_SHIFT)
            {
                inputSystem.SubmitKey(KeyCode::LeftShift, (GetKeyState(VK_LSHIFT) & 0x8000) != 0);
                inputSystem.SubmitKey(KeyCode::RightShift, (GetKeyState(VK_RSHIFT) & 0x8000) != 0);
            }
            else if (wParam == VK_CONTROL)
            {
                inputSystem.SubmitKey(KeyCode::LeftControl, (GetKeyState(VK_LCONTROL) & 0x8000) != 0);
                inputSystem.SubmitKey(KeyCode::RightControl, (GetKeyState(VK_RCONTROL) & 0x8000) != 0);
            }
            else if (wParam == VK_MENU)
            {
                inputSystem.SubmitKey(KeyCode::LeftAlt, (GetKeyState(VK_LMENU) & 0x8000) != 0);
                inputSystem.SubmitKey(KeyCode::RightAlt, (GetKeyState(VK_RMENU) & 0x8000) != 0);
            }
            else
            {
                inputSystem.SubmitKey(MapVirtualKeyToKeyCode(wParam, lParam), false);
            }
            return false;
        case WM_MOUSEMOVE:
            SubmitClientMousePosition(inputSystem, lParam);
            return false;
        case WM_LBUTTONDOWN:
            SubmitClientMousePosition(inputSystem, lParam);
            inputSystem.SubmitMouseButton(MouseButton::Left, true);
            SetCapture(window);
            return false;
        case WM_LBUTTONUP:
            SubmitClientMousePosition(inputSystem, lParam);
            inputSystem.SubmitMouseButton(MouseButton::Left, false);
            ReleaseCapture();
            return false;
        case WM_RBUTTONDOWN:
            SubmitClientMousePosition(inputSystem, lParam);
            inputSystem.SubmitMouseButton(MouseButton::Right, true);
            SetCapture(window);
            return false;
        case WM_RBUTTONUP:
            SubmitClientMousePosition(inputSystem, lParam);
            inputSystem.SubmitMouseButton(MouseButton::Right, false);
            ReleaseCapture();
            return false;
        case WM_MBUTTONDOWN:
            SubmitClientMousePosition(inputSystem, lParam);
            inputSystem.SubmitMouseButton(MouseButton::Middle, true);
            SetCapture(window);
            return false;
        case WM_MBUTTONUP:
            SubmitClientMousePosition(inputSystem, lParam);
            inputSystem.SubmitMouseButton(MouseButton::Middle, false);
            ReleaseCapture();
            return false;
        case WM_XBUTTONDOWN:
            SubmitClientMousePosition(inputSystem, lParam);
            inputSystem.SubmitMouseButton(HIWORD(wParam) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2, true);
            SetCapture(window);
            result = TRUE;
            return true;
        case WM_XBUTTONUP:
            SubmitClientMousePosition(inputSystem, lParam);
            inputSystem.SubmitMouseButton(HIWORD(wParam) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2, false);
            ReleaseCapture();
            result = TRUE;
            return true;
        case WM_MOUSEWHEEL:
            inputSystem.SubmitMouseWheel(static_cast<Float32>(GET_WHEEL_DELTA_WPARAM(wParam)) /
                                         static_cast<Float32>(WHEEL_DELTA));
            return false;
        default:
            return false;
        }
    }
} // namespace ve
