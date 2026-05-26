#include "Engine/Runtime/Platform/Windows/Win32Window.h"

#include "Engine/Runtime/Platform/Windows/Win32DebugConsole.h"

#include <algorithm>
#include <vector>

namespace ve
{
    namespace
    {
        constexpr wchar_t WindowClassName[] = L"VEngineWin32Window";

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

    void Win32Window::SetCommandHandler(WindowCommandHandler handler)
    {
        commandHandler_ = std::move(handler);
        SetWin32DebugConsoleCommandHandler(commandHandler_);
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

    void Win32Window::SetNativeMessageHandler(Win32NativeMessageHandler handler)
    {
        nativeMessageHandler_ = std::move(handler);
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

        CreateMenuBar(desc);
        UpdateClientExtent();

        if (desc.visible)
        {
            Show();
        }

        return ErrorCode::None;
    }

    void Win32Window::CreateMenuBar(const WindowDesc& desc)
    {
        if (windowHandle_ == nullptr || desc.menuItems.empty())
        {
            return;
        }

        HMENU menuBar = CreateMenu();
        if (menuBar == nullptr)
        {
            return;
        }

        std::vector<std::pair<std::string, HMENU>> menus;
        UINT nextMenuId = 1000;

        for (const WindowMenuItemDesc& item : desc.menuItems)
        {
            if (item.menu.empty() || item.label.empty() || item.command.empty())
            {
                continue;
            }

            HMENU submenu = nullptr;
            auto menuIter = std::find_if(menus.begin(),
                                         menus.end(),
                                         [&item](const std::pair<std::string, HMENU>& menu)
                                         { return menu.first == item.menu; });
            if (menuIter == menus.end())
            {
                submenu = CreatePopupMenu();
                if (submenu == nullptr)
                {
                    continue;
                }

                const std::wstring wideMenu = Utf8ToWide(item.menu);
                AppendMenuW(menuBar, MF_POPUP, reinterpret_cast<UINT_PTR>(submenu), wideMenu.c_str());
                menus.emplace_back(item.menu, submenu);
            }
            else
            {
                submenu = menuIter->second;
            }

            const UINT menuId = nextMenuId++;
            const std::wstring wideLabel = Utf8ToWide(item.label);
            AppendMenuW(submenu, MF_STRING, menuId, wideLabel.c_str());
            menuCommands_.emplace(menuId, item.command);
        }

        if (SetMenu(windowHandle_, menuBar) == 0)
        {
            DestroyMenu(menuBar);
            menuCommands_.clear();
            return;
        }

        DrawMenuBar(windowHandle_);
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
        if (nativeMessageHandler_)
        {
            LRESULT handlerResult = 0;
            if (nativeMessageHandler_(windowHandle, message, wParam, lParam, handlerResult))
            {
                return handlerResult;
            }
        }

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
        case WM_COMMAND:
        {
            const UINT commandId = LOWORD(wParam);
            const auto commandIter = menuCommands_.find(commandId);
            if (commandIter != menuCommands_.end())
            {
                if (commandHandler_)
                {
                    commandHandler_(commandIter->second);
                }

                return 0;
            }

            break;
        }
        case WM_SETFOCUS:
            focused_ = true;
            return 0;
        case WM_KILLFOCUS:
            focused_ = false;
            return 0;
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
} // namespace ve
