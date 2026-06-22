#include "Editor/Core/EditorInput.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <imgui.h>

#if VE_PLATFORM_WINDOWS
#include <Windows.h>
#include <backends/imgui_impl_win32.h>
#endif

namespace ve::editor
{
    namespace
    {
#if VE_PLATFORM_WINDOWS
        [[nodiscard]] ImGuiKey WindowsKeyCodeToImGuiKey(UInt32 keyCode, UInt32 scanCode, bool isExtended) noexcept
        {
            if (keyCode == VK_RETURN && isExtended)
            {
                return ImGuiKey_KeypadEnter;
            }

            switch (keyCode)
            {
            case VK_TAB:
                return ImGuiKey_Tab;
            case VK_LEFT:
                return ImGuiKey_LeftArrow;
            case VK_RIGHT:
                return ImGuiKey_RightArrow;
            case VK_UP:
                return ImGuiKey_UpArrow;
            case VK_DOWN:
                return ImGuiKey_DownArrow;
            case VK_PRIOR:
                return ImGuiKey_PageUp;
            case VK_NEXT:
                return ImGuiKey_PageDown;
            case VK_HOME:
                return ImGuiKey_Home;
            case VK_END:
                return ImGuiKey_End;
            case VK_INSERT:
                return ImGuiKey_Insert;
            case VK_DELETE:
                return ImGuiKey_Delete;
            case VK_BACK:
                return ImGuiKey_Backspace;
            case VK_SPACE:
                return ImGuiKey_Space;
            case VK_RETURN:
                return ImGuiKey_Enter;
            case VK_ESCAPE:
                return ImGuiKey_Escape;
            case VK_OEM_COMMA:
                return ImGuiKey_Comma;
            case VK_OEM_PERIOD:
                return ImGuiKey_Period;
            case VK_CAPITAL:
                return ImGuiKey_CapsLock;
            case VK_SCROLL:
                return ImGuiKey_ScrollLock;
            case VK_NUMLOCK:
                return ImGuiKey_NumLock;
            case VK_SNAPSHOT:
                return ImGuiKey_PrintScreen;
            case VK_PAUSE:
                return ImGuiKey_Pause;
            case VK_NUMPAD0:
                return ImGuiKey_Keypad0;
            case VK_NUMPAD1:
                return ImGuiKey_Keypad1;
            case VK_NUMPAD2:
                return ImGuiKey_Keypad2;
            case VK_NUMPAD3:
                return ImGuiKey_Keypad3;
            case VK_NUMPAD4:
                return ImGuiKey_Keypad4;
            case VK_NUMPAD5:
                return ImGuiKey_Keypad5;
            case VK_NUMPAD6:
                return ImGuiKey_Keypad6;
            case VK_NUMPAD7:
                return ImGuiKey_Keypad7;
            case VK_NUMPAD8:
                return ImGuiKey_Keypad8;
            case VK_NUMPAD9:
                return ImGuiKey_Keypad9;
            case VK_DECIMAL:
                return ImGuiKey_KeypadDecimal;
            case VK_DIVIDE:
                return ImGuiKey_KeypadDivide;
            case VK_MULTIPLY:
                return ImGuiKey_KeypadMultiply;
            case VK_SUBTRACT:
                return ImGuiKey_KeypadSubtract;
            case VK_ADD:
                return ImGuiKey_KeypadAdd;
            case VK_LSHIFT:
                return ImGuiKey_LeftShift;
            case VK_LCONTROL:
                return ImGuiKey_LeftCtrl;
            case VK_LMENU:
                return ImGuiKey_LeftAlt;
            case VK_LWIN:
                return ImGuiKey_LeftSuper;
            case VK_RSHIFT:
                return ImGuiKey_RightShift;
            case VK_RCONTROL:
                return ImGuiKey_RightCtrl;
            case VK_RMENU:
                return ImGuiKey_RightAlt;
            case VK_RWIN:
                return ImGuiKey_RightSuper;
            case VK_APPS:
                return ImGuiKey_Menu;
            case VK_BROWSER_BACK:
                return ImGuiKey_AppBack;
            case VK_BROWSER_FORWARD:
                return ImGuiKey_AppForward;
            default:
                break;
            }

            if (keyCode >= '0' && keyCode <= '9')
            {
                return static_cast<ImGuiKey>(ImGuiKey_0 + (keyCode - '0'));
            }

            if (keyCode >= 'A' && keyCode <= 'Z')
            {
                return static_cast<ImGuiKey>(ImGuiKey_A + (keyCode - 'A'));
            }

            if (keyCode >= VK_F1 && keyCode <= VK_F24)
            {
                return static_cast<ImGuiKey>(ImGuiKey_F1 + (keyCode - VK_F1));
            }

            switch (scanCode)
            {
            case 41:
                return ImGuiKey_GraveAccent;
            case 12:
                return ImGuiKey_Minus;
            case 13:
                return ImGuiKey_Equal;
            case 26:
                return ImGuiKey_LeftBracket;
            case 27:
                return ImGuiKey_RightBracket;
            case 86:
                return ImGuiKey_Oem102;
            case 43:
                return ImGuiKey_Backslash;
            case 39:
                return ImGuiKey_Semicolon;
            case 40:
                return ImGuiKey_Apostrophe;
            case 51:
                return ImGuiKey_Comma;
            case 52:
                return ImGuiKey_Period;
            case 53:
                return ImGuiKey_Slash;
            default:
                break;
            }

            return ImGuiKey_None;
        }
#endif

        [[nodiscard]] int MouseButtonToImGuiButton(InputMouseButton button) noexcept
        {
            switch (button)
            {
            case InputMouseButton::Left:
                return 0;
            case InputMouseButton::Right:
                return 1;
            case InputMouseButton::Middle:
                return 2;
            case InputMouseButton::X1:
                return 3;
            case InputMouseButton::X2:
                return 4;
            case InputMouseButton::Count:
                break;
            }

            return 0;
        }

        void SubmitModifierEvents(ImGuiIO& io, InputModifierFlags modifiers)
        {
            io.AddKeyEvent(ImGuiMod_Ctrl, HasInputModifier(modifiers, InputModifierFlags::Control));
            io.AddKeyEvent(ImGuiMod_Shift, HasInputModifier(modifiers, InputModifierFlags::Shift));
            io.AddKeyEvent(ImGuiMod_Alt, HasInputModifier(modifiers, InputModifierFlags::Alt));
            io.AddKeyEvent(ImGuiMod_Super, HasInputModifier(modifiers, InputModifierFlags::Super));
        }
    } // namespace

    EditorInput::~EditorInput()
    {
        Shutdown();
    }

    ErrorCode EditorInput::Init(void* nativeWindowHandle)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        if (nativeWindowHandle == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }

#if VE_PLATFORM_WINDOWS
        if (!ImGui_ImplWin32_Init(nativeWindowHandle))
        {
            return ErrorCode::PlatformError;
        }

        initialized_ = true;
        return ErrorCode::None;
#else
        (void)nativeWindowHandle;
        return ErrorCode::Unsupported;
#endif
    }

    void EditorInput::StartFrame()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_)
        {
            return;
        }

#if VE_PLATFORM_WINDOWS
        ImGui_ImplWin32_NewFrame();
#endif
    }

    void EditorInput::BeginOSEventFrame() noexcept
    {
        mouseDeltaX_ = 0;
        mouseDeltaY_ = 0;
    }

    bool EditorInput::OnOSEvent(const OSEvent& event)
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_)
        {
            return false;
        }

        ImGuiIO& io = ImGui::GetIO();

        switch (event.type)
        {
        case OSEventType::WindowFocusGained:
            io.AddFocusEvent(true);
            return true;
        case OSEventType::WindowFocusLost:
            io.AddFocusEvent(false);
            return true;
        case OSEventType::KeyboardKeyDown:
        case OSEventType::KeyboardKeyUp:
        {
            SubmitModifierEvents(io, event.modifiers);
#if VE_PLATFORM_WINDOWS
            const ImGuiKey imguiKey = WindowsKeyCodeToImGuiKey(event.keyCode, event.scanCode, event.isExtended);
            if (imguiKey != ImGuiKey_None)
            {
                io.AddKeyEvent(imguiKey, event.type == OSEventType::KeyboardKeyDown);
            }
#endif
            return false;
        }
        case OSEventType::TextInput:
            io.AddInputCharacterUTF16(static_cast<ImWchar16>(event.textCodepoint));
            return false;
        case OSEventType::MouseMoved:
            StoreMousePosition(event);
            io.AddMousePosEvent(static_cast<float>(event.mouseX), static_cast<float>(event.mouseY));
            return false;
        case OSEventType::MouseButtonDown:
        case OSEventType::MouseButtonUp:
            StoreMousePosition(event);
            io.AddMousePosEvent(static_cast<float>(event.mouseX), static_cast<float>(event.mouseY));
            io.AddMouseButtonEvent(MouseButtonToImGuiButton(event.mouseButton), event.type == OSEventType::MouseButtonDown);
            return false;
        case OSEventType::MouseWheel:
            StoreMousePosition(event);
            io.AddMousePosEvent(static_cast<float>(event.mouseX), static_cast<float>(event.mouseY));
            io.AddMouseWheelEvent(event.mouseWheelX, event.mouseWheelY);
            return false;
        case OSEventType::WindowMinimized:
        case OSEventType::WindowRestored:
        case OSEventType::WindowResized:
        case OSEventType::WindowShown:
        case OSEventType::WindowHidden:
        case OSEventType::FrameEndFenceSignal:
            return false;
        }

        return false;
    }

    void EditorInput::StoreMousePosition(const OSEvent& event) noexcept
    {
        if (hasMousePosition_)
        {
            mouseDeltaX_ += event.mouseX - mouseX_;
            mouseDeltaY_ += event.mouseY - mouseY_;
        }

        mouseX_ = event.mouseX;
        mouseY_ = event.mouseY;
        hasMousePosition_ = true;
    }

    void EditorInput::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

#if VE_PLATFORM_WINDOWS
        ImGui_ImplWin32_Shutdown();
#endif
        initialized_ = false;
        hasMousePosition_ = false;
        mouseDeltaX_ = 0;
        mouseDeltaY_ = 0;
    }

    bool EditorInput::IsInitialized() const noexcept
    {
        return initialized_;
    }

    Int32 EditorInput::GetMouseDeltaX() const noexcept
    {
        return mouseDeltaX_;
    }

    Int32 EditorInput::GetMouseDeltaY() const noexcept
    {
        return mouseDeltaY_;
    }
} // namespace ve::editor
