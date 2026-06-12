#include "Engine/Runtime/Platform/Windows/Win32Window.h"

#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"

#include <algorithm>

namespace ve
{
    namespace
    {
        constexpr wchar_t WindowClassName[] = L"VEngineWin32Window";

        [[nodiscard]] Int32 GetSignedLowWord(LPARAM value) noexcept
        {
            return static_cast<Int16>(value & 0xffff);
        }

        [[nodiscard]] Int32 GetSignedHighWord(LPARAM value) noexcept
        {
            return static_cast<Int16>((value >> 16) & 0xffff);
        }

        [[nodiscard]] Int16 GetSignedHighWord(WPARAM value) noexcept
        {
            return static_cast<Int16>((value >> 16) & 0xffff);
        }

        [[nodiscard]] InputModifierFlags GetCurrentInputModifiers() noexcept
        {
            InputModifierFlags modifiers = InputModifierFlags::None;

            if ((GetKeyState(VK_SHIFT) & 0x8000) != 0)
            {
                modifiers |= InputModifierFlags::Shift;
            }

            if ((GetKeyState(VK_CONTROL) & 0x8000) != 0)
            {
                modifiers |= InputModifierFlags::Control;
            }

            if ((GetKeyState(VK_MENU) & 0x8000) != 0)
            {
                modifiers |= InputModifierFlags::Alt;
            }

            if ((GetKeyState(VK_LWIN) & 0x8000) != 0 || (GetKeyState(VK_RWIN) & 0x8000) != 0)
            {
                modifiers |= InputModifierFlags::Super;
            }

            if ((GetKeyState(VK_CAPITAL) & 0x0001) != 0)
            {
                modifiers |= InputModifierFlags::CapsLock;
            }

            if ((GetKeyState(VK_NUMLOCK) & 0x0001) != 0)
            {
                modifiers |= InputModifierFlags::NumLock;
            }

            return modifiers;
        }

        [[nodiscard]] OSEvent MakeKeyboardEvent(OSEventType type, WPARAM wParam, LPARAM lParam)
        {
            OSEvent event;
            event.type = type;
            event.keyCode = static_cast<UInt32>(wParam);
            event.scanCode = static_cast<UInt32>((lParam >> 16) & 0xff);
            event.modifiers = GetCurrentInputModifiers();
            event.isRepeat = (type == OSEventType::KeyboardKeyDown) && ((lParam & (1ll << 30)) != 0);
            event.isExtended = (lParam & (1ll << 24)) != 0;

            if (wParam == VK_SHIFT)
            {
                event.keyCode = MapVirtualKeyW(event.scanCode, MAPVK_VSC_TO_VK_EX);
            }
            else if (wParam == VK_CONTROL)
            {
                event.keyCode = event.isExtended ? VK_RCONTROL : VK_LCONTROL;
            }
            else if (wParam == VK_MENU)
            {
                event.keyCode = event.isExtended ? VK_RMENU : VK_LMENU;
            }

            return event;
        }

        [[nodiscard]] OSEvent MakeMousePositionEvent(OSEventType type, LPARAM lParam)
        {
            OSEvent event;
            event.type = type;
            event.mouseX = GetSignedLowWord(lParam);
            event.mouseY = GetSignedHighWord(lParam);
            event.modifiers = GetCurrentInputModifiers();
            return event;
        }

        [[nodiscard]] bool HasPressedMouseButton(WPARAM wParam) noexcept
        {
            return (wParam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON | MK_XBUTTON1 | MK_XBUTTON2)) != 0;
        }

        std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredLength = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);

            if (requiredLength <= 0)
            {
                return L"VEngine";
            }

            std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
            MultiByteToWideChar(CP_UTF8,
                                MB_ERR_INVALID_CHARS,
                                text.data(),
                                static_cast<int>(text.size()),
                                wideText.data(),
                                requiredLength);

            return wideText;
        }

        ErrorCode RegisterWindowClass(WNDPROC windowProc)
        {
            HINSTANCE instance = GetModuleHandleW(nullptr);

            WNDCLASSEXW windowClass = {};
            windowClass.cbSize = sizeof(windowClass);
            windowClass.style = CS_HREDRAW | CS_VREDRAW;
            windowClass.lpfnWndProc = windowProc;
            windowClass.hInstance = instance;
            windowClass.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
            windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
            windowClass.lpszClassName = WindowClassName;
            windowClass.hIconSm = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));

            if (RegisterClassExW(&windowClass) == 0)
            {
                const DWORD errorCode = GetLastError();

                if (errorCode != ERROR_CLASS_ALREADY_EXISTS)
                {
                    return ErrorCode::PlatformError;
                }
            }

            return ErrorCode::None;
        }
    } // namespace

    Win32Window::~Win32Window()
    {
        Close();
    }

    Result<std::unique_ptr<Win32Window>> Win32Window::Create(const WindowDesc& desc)
    {
        auto window = std::unique_ptr<Win32Window>(new Win32Window());
        ErrorCode result = window->Initialize(desc);

        if (result != ErrorCode::None)
        {
            return Result<std::unique_ptr<Win32Window>>::Failure(Error(result));
        }

        return Result<std::unique_ptr<Win32Window>>::Success(std::move(window));
    }

    Result<std::unique_ptr<Window>> Win32Window::CreatePlatformWindow(const WindowDesc& desc)
    {
        Result<std::unique_ptr<Win32Window>> windowResult = Create(desc);

        if (!windowResult)
        {
            return Result<std::unique_ptr<Window>>::Failure(windowResult.GetError());
        }

        return Result<std::unique_ptr<Window>>::Success(windowResult.MoveValue());
    }

    void Win32Window::Show()
    {
        if (windowHandle_ == nullptr)
        {
            return;
        }

        ShowWindow(static_cast<HWND>(windowHandle_), SW_SHOW);
        UpdateWindow(static_cast<HWND>(windowHandle_));
        visible_ = true;
    }

    void Win32Window::Close()
    {
        if (windowHandle_ == nullptr)
        {
            return;
        }

        shouldClose_ = true;
        DestroyWindow(static_cast<HWND>(windowHandle_));
    }

    WindowPumpStatus Win32Window::PumpEvents()
    {
        const Win32MessageLoop::PumpResult result = messageLoop_.PumpPendingMessages();

        if (result == Win32MessageLoop::PumpResult::Quit)
        {
            shouldClose_ = true;
            return WindowPumpStatus{WindowPumpResult::Quit, messageLoop_.GetQuitExitCode()};
        }

        return WindowPumpStatus{};
    }

    bool Win32Window::TryPopOSEvent(OSEvent& outEvent)
    {
        if (pendingOSEvents_.empty())
        {
            return false;
        }

        outEvent = pendingOSEvents_.front();
        pendingOSEvents_.pop_front();
        return true;
    }

    void Win32Window::SetCommandHandler(WindowCommandHandler handler)
    {
        SetWin32DebugConsoleCommandHandler(std::move(handler));
    }

    void Win32Window::PumpCommands()
    {
        PumpWin32DebugConsoleCommands();
    }

    bool Win32Window::ShouldClose() const noexcept
    {
        return shouldClose_;
    }

    bool Win32Window::IsVisible() const noexcept
    {
        return visible_;
    }

    bool Win32Window::IsFocused() const noexcept
    {
        return focused_;
    }

    bool Win32Window::IsMinimized() const noexcept
    {
        return minimized_;
    }

    WindowExtent Win32Window::GetClientExtent() const noexcept
    {
        return clientExtent_;
    }

    const std::string& Win32Window::GetTitle() const noexcept
    {
        return title_;
    }

    void* Win32Window::GetNativeHandle() const noexcept
    {
        return windowHandle_;
    }

    void* Win32Window::GetNativeLayer() const noexcept
    {
        return nullptr;
    }

    HWND Win32Window::GetWin32Handle() const noexcept
    {
        return windowHandle_;
    }

    ErrorCode Win32Window::Initialize(const WindowDesc& desc)
    {
        ErrorCode registerResult = RegisterWindowClass(&Win32Window::WindowProc);

        if (registerResult != ErrorCode::None)
        {
            return registerResult;
        }

        title_ = desc.title.empty() ? "VEngine" : desc.title;
        clientExtent_.width = std::max(desc.width, 1u);
        clientExtent_.height = std::max(desc.height, 1u);

        const DWORD style = WS_OVERLAPPEDWINDOW;
        const DWORD extendedStyle = 0;
        RECT windowRect = {
            0,
            0,
            static_cast<LONG>(clientExtent_.width),
            static_cast<LONG>(clientExtent_.height),
        };

        if (AdjustWindowRectEx(&windowRect, style, FALSE, extendedStyle) == 0)
        {
            return ErrorCode::PlatformError;
        }

        const std::wstring wideTitle = Utf8ToWide(title_);
        windowHandle_ = CreateWindowExW(extendedStyle,
                                        WindowClassName,
                                        wideTitle.c_str(),
                                        style,
                                        CW_USEDEFAULT,
                                        CW_USEDEFAULT,
                                        windowRect.right - windowRect.left,
                                        windowRect.bottom - windowRect.top,
                                        nullptr,
                                        nullptr,
                                        GetModuleHandleW(nullptr),
                                        this);

        if (windowHandle_ == nullptr)
        {
            return ErrorCode::PlatformError;
        }

        UpdateClientExtent();

        if (desc.visible)
        {
            Show();
        }

        return ErrorCode::None;
    }

    LRESULT CALLBACK Win32Window::WindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        Win32Window* window = nullptr;

        if (message == WM_NCCREATE)
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            window = static_cast<Win32Window*>(createStruct->lpCreateParams);
            window->windowHandle_ = windowHandle;
            SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }
        else
        {
            window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
        }

        if (window != nullptr)
        {
            return window->HandleMessage(windowHandle, message, wParam, lParam);
        }

        return DefWindowProcW(windowHandle, message, wParam, lParam);
    }

    LRESULT Win32Window::HandleMessage(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CLOSE:
            shouldClose_ = true;
            DestroyWindow(windowHandle);
            return 0;
        case WM_DESTROY:
            shouldClose_ = true;
            visible_ = false;
            PostQuitMessage(0);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(windowHandle, GWLP_USERDATA, 0);
            windowHandle_ = nullptr;
            return DefWindowProcW(windowHandle, message, wParam, lParam);
        case WM_SHOWWINDOW:
            visible_ = wParam != 0;
            return 0;
        case WM_SIZE:
            minimized_ = wParam == SIZE_MINIMIZED;
            clientExtent_.width = LOWORD(lParam);
            clientExtent_.height = HIWORD(lParam);
            return 0;
        case WM_ACTIVATE:
            focused_ = LOWORD(wParam) != WA_INACTIVE;
            return 0;
        case WM_SETFOCUS:
            focused_ = true;
            return 0;
        case WM_KILLFOCUS:
            focused_ = false;
            return 0;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            QueueOSEvent(MakeKeyboardEvent(OSEventType::KeyboardKeyDown, wParam, lParam));
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            QueueOSEvent(MakeKeyboardEvent(OSEventType::KeyboardKeyUp, wParam, lParam));
            break;
        case WM_CHAR:
            QueueOSEvent(OSEvent{
                .type = OSEventType::TextInput,
                .modifiers = GetCurrentInputModifiers(),
                .textCodepoint = static_cast<UInt32>(wParam),
            });
            break;
        case WM_MOUSEMOVE:
            QueueOSEvent(MakeMousePositionEvent(OSEventType::MouseMoved, lParam));
            break;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_XBUTTONDOWN:
        {
            OSEvent event = MakeMousePositionEvent(OSEventType::MouseButtonDown, lParam);
            if (message == WM_LBUTTONDOWN)
            {
                event.mouseButton = InputMouseButton::Left;
            }
            else if (message == WM_RBUTTONDOWN)
            {
                event.mouseButton = InputMouseButton::Right;
            }
            else if (message == WM_MBUTTONDOWN)
            {
                event.mouseButton = InputMouseButton::Middle;
            }
            else
            {
                event.mouseButton =
                    GetSignedHighWord(wParam) == XBUTTON1 ? InputMouseButton::X1 : InputMouseButton::X2;
            }

            SetCapture(windowHandle);
            QueueOSEvent(event);
            return 0;
        }
        case WM_LBUTTONUP:
        case WM_RBUTTONUP:
        case WM_MBUTTONUP:
        case WM_XBUTTONUP:
        {
            OSEvent event = MakeMousePositionEvent(OSEventType::MouseButtonUp, lParam);
            if (message == WM_LBUTTONUP)
            {
                event.mouseButton = InputMouseButton::Left;
            }
            else if (message == WM_RBUTTONUP)
            {
                event.mouseButton = InputMouseButton::Right;
            }
            else if (message == WM_MBUTTONUP)
            {
                event.mouseButton = InputMouseButton::Middle;
            }
            else
            {
                event.mouseButton =
                    GetSignedHighWord(wParam) == XBUTTON1 ? InputMouseButton::X1 : InputMouseButton::X2;
            }

            QueueOSEvent(event);
            if (!HasPressedMouseButton(wParam))
            {
                ReleaseCapture();
            }
            return 0;
        }
        case WM_MOUSEWHEEL:
        {
            POINT point{GetSignedLowWord(lParam), GetSignedHighWord(lParam)};
            ScreenToClient(windowHandle, &point);

            OSEvent event;
            event.type = OSEventType::MouseWheel;
            event.modifiers = GetCurrentInputModifiers();
            event.mouseX = static_cast<Int32>(point.x);
            event.mouseY = static_cast<Int32>(point.y);
            event.mouseWheelY = static_cast<Float32>(GetSignedHighWord(wParam)) / static_cast<Float32>(WHEEL_DELTA);
            QueueOSEvent(event);
            return 0;
        }
        case WM_MOUSEHWHEEL:
        {
            POINT point{GetSignedLowWord(lParam), GetSignedHighWord(lParam)};
            ScreenToClient(windowHandle, &point);

            OSEvent event;
            event.type = OSEventType::MouseWheel;
            event.modifiers = GetCurrentInputModifiers();
            event.mouseX = static_cast<Int32>(point.x);
            event.mouseY = static_cast<Int32>(point.y);
            event.mouseWheelX = static_cast<Float32>(GetSignedHighWord(wParam)) / static_cast<Float32>(WHEEL_DELTA);
            QueueOSEvent(event);
            return 0;
        }
        default:
            break;
        }

        return DefWindowProcW(windowHandle, message, wParam, lParam);
    }

    void Win32Window::UpdateClientExtent() noexcept
    {
        if (windowHandle_ == nullptr)
        {
            clientExtent_ = {};
            return;
        }

        RECT clientRect = {};

        if (GetClientRect(windowHandle_, &clientRect) == 0)
        {
            return;
        }

        clientExtent_.width = static_cast<uint32_t>(std::max<LONG>(clientRect.right - clientRect.left, 0));
        clientExtent_.height = static_cast<uint32_t>(std::max<LONG>(clientRect.bottom - clientRect.top, 0));
    }

    void Win32Window::QueueOSEvent(OSEvent event)
    {
        pendingOSEvents_.push_back(event);
    }
} // namespace ve
