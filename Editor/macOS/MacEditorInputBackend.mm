#include "Editor/macOS/MacEditorInputBackend.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <imgui.h>

#import <AppKit/AppKit.h>
#import <Carbon/Carbon.h>

#include <chrono>
#include <memory>

namespace ve::editor
{
    namespace
    {
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

        [[nodiscard]] ImGuiKey MacKeyCodeToImGuiKey(UInt32 keyCode) noexcept
        {
            switch (keyCode)
            {
            case kVK_ANSI_A:
                return ImGuiKey_A;
            case kVK_ANSI_S:
                return ImGuiKey_S;
            case kVK_ANSI_D:
                return ImGuiKey_D;
            case kVK_ANSI_F:
                return ImGuiKey_F;
            case kVK_ANSI_H:
                return ImGuiKey_H;
            case kVK_ANSI_G:
                return ImGuiKey_G;
            case kVK_ANSI_Z:
                return ImGuiKey_Z;
            case kVK_ANSI_X:
                return ImGuiKey_X;
            case kVK_ANSI_C:
                return ImGuiKey_C;
            case kVK_ANSI_V:
                return ImGuiKey_V;
            case kVK_ANSI_B:
                return ImGuiKey_B;
            case kVK_ANSI_Q:
                return ImGuiKey_Q;
            case kVK_ANSI_W:
                return ImGuiKey_W;
            case kVK_ANSI_E:
                return ImGuiKey_E;
            case kVK_ANSI_R:
                return ImGuiKey_R;
            case kVK_ANSI_Y:
                return ImGuiKey_Y;
            case kVK_ANSI_T:
                return ImGuiKey_T;
            case kVK_ANSI_1:
                return ImGuiKey_1;
            case kVK_ANSI_2:
                return ImGuiKey_2;
            case kVK_ANSI_3:
                return ImGuiKey_3;
            case kVK_ANSI_4:
                return ImGuiKey_4;
            case kVK_ANSI_5:
                return ImGuiKey_5;
            case kVK_ANSI_6:
                return ImGuiKey_6;
            case kVK_ANSI_7:
                return ImGuiKey_7;
            case kVK_ANSI_8:
                return ImGuiKey_8;
            case kVK_ANSI_9:
                return ImGuiKey_9;
            case kVK_ANSI_0:
                return ImGuiKey_0;
            case kVK_ANSI_Equal:
                return ImGuiKey_Equal;
            case kVK_ANSI_Minus:
                return ImGuiKey_Minus;
            case kVK_ANSI_RightBracket:
                return ImGuiKey_RightBracket;
            case kVK_ANSI_O:
                return ImGuiKey_O;
            case kVK_ANSI_U:
                return ImGuiKey_U;
            case kVK_ANSI_LeftBracket:
                return ImGuiKey_LeftBracket;
            case kVK_ANSI_I:
                return ImGuiKey_I;
            case kVK_ANSI_P:
                return ImGuiKey_P;
            case kVK_ANSI_L:
                return ImGuiKey_L;
            case kVK_ANSI_J:
                return ImGuiKey_J;
            case kVK_ANSI_Quote:
                return ImGuiKey_Apostrophe;
            case kVK_ANSI_K:
                return ImGuiKey_K;
            case kVK_ANSI_Semicolon:
                return ImGuiKey_Semicolon;
            case kVK_ANSI_Backslash:
                return ImGuiKey_Backslash;
            case kVK_ANSI_Comma:
                return ImGuiKey_Comma;
            case kVK_ANSI_Slash:
                return ImGuiKey_Slash;
            case kVK_ANSI_N:
                return ImGuiKey_N;
            case kVK_ANSI_M:
                return ImGuiKey_M;
            case kVK_ANSI_Period:
                return ImGuiKey_Period;
            case kVK_ANSI_Grave:
                return ImGuiKey_GraveAccent;
            case kVK_ANSI_KeypadDecimal:
                return ImGuiKey_KeypadDecimal;
            case kVK_ANSI_KeypadMultiply:
                return ImGuiKey_KeypadMultiply;
            case kVK_ANSI_KeypadPlus:
                return ImGuiKey_KeypadAdd;
            case kVK_ANSI_KeypadClear:
                return ImGuiKey_NumLock;
            case kVK_ANSI_KeypadDivide:
                return ImGuiKey_KeypadDivide;
            case kVK_ANSI_KeypadEnter:
                return ImGuiKey_KeypadEnter;
            case kVK_ANSI_KeypadMinus:
                return ImGuiKey_KeypadSubtract;
            case kVK_ANSI_KeypadEquals:
                return ImGuiKey_KeypadEqual;
            case kVK_ANSI_Keypad0:
                return ImGuiKey_Keypad0;
            case kVK_ANSI_Keypad1:
                return ImGuiKey_Keypad1;
            case kVK_ANSI_Keypad2:
                return ImGuiKey_Keypad2;
            case kVK_ANSI_Keypad3:
                return ImGuiKey_Keypad3;
            case kVK_ANSI_Keypad4:
                return ImGuiKey_Keypad4;
            case kVK_ANSI_Keypad5:
                return ImGuiKey_Keypad5;
            case kVK_ANSI_Keypad6:
                return ImGuiKey_Keypad6;
            case kVK_ANSI_Keypad7:
                return ImGuiKey_Keypad7;
            case kVK_ANSI_Keypad8:
                return ImGuiKey_Keypad8;
            case kVK_ANSI_Keypad9:
                return ImGuiKey_Keypad9;
            case kVK_Return:
                return ImGuiKey_Enter;
            case kVK_Tab:
                return ImGuiKey_Tab;
            case kVK_Space:
                return ImGuiKey_Space;
            case kVK_Delete:
                return ImGuiKey_Backspace;
            case kVK_Escape:
                return ImGuiKey_Escape;
            case kVK_CapsLock:
                return ImGuiKey_CapsLock;
            case kVK_Control:
                return ImGuiKey_LeftCtrl;
            case kVK_Shift:
                return ImGuiKey_LeftShift;
            case kVK_Option:
                return ImGuiKey_LeftAlt;
            case kVK_Command:
                return ImGuiKey_LeftSuper;
            case kVK_RightControl:
                return ImGuiKey_RightCtrl;
            case kVK_RightShift:
                return ImGuiKey_RightShift;
            case kVK_RightOption:
                return ImGuiKey_RightAlt;
            case kVK_RightCommand:
                return ImGuiKey_RightSuper;
            case kVK_F1:
                return ImGuiKey_F1;
            case kVK_F2:
                return ImGuiKey_F2;
            case kVK_F3:
                return ImGuiKey_F3;
            case kVK_F4:
                return ImGuiKey_F4;
            case kVK_F5:
                return ImGuiKey_F5;
            case kVK_F6:
                return ImGuiKey_F6;
            case kVK_F7:
                return ImGuiKey_F7;
            case kVK_F8:
                return ImGuiKey_F8;
            case kVK_F9:
                return ImGuiKey_F9;
            case kVK_F10:
                return ImGuiKey_F10;
            case kVK_F11:
                return ImGuiKey_F11;
            case kVK_F12:
                return ImGuiKey_F12;
            case kVK_F13:
                return ImGuiKey_F13;
            case kVK_F14:
                return ImGuiKey_F14;
            case kVK_F15:
                return ImGuiKey_F15;
            case kVK_F16:
                return ImGuiKey_F16;
            case kVK_F17:
                return ImGuiKey_F17;
            case kVK_F18:
                return ImGuiKey_F18;
            case kVK_F19:
                return ImGuiKey_F19;
            case kVK_F20:
                return ImGuiKey_F20;
            case kVK_Help:
                return ImGuiKey_Insert;
            case kVK_Home:
                return ImGuiKey_Home;
            case kVK_PageUp:
                return ImGuiKey_PageUp;
            case kVK_ForwardDelete:
                return ImGuiKey_Delete;
            case kVK_End:
                return ImGuiKey_End;
            case kVK_PageDown:
                return ImGuiKey_PageDown;
            case kVK_LeftArrow:
                return ImGuiKey_LeftArrow;
            case kVK_RightArrow:
                return ImGuiKey_RightArrow;
            case kVK_DownArrow:
                return ImGuiKey_DownArrow;
            case kVK_UpArrow:
                return ImGuiKey_UpArrow;
            default:
                return ImGuiKey_None;
            }
        }

        [[nodiscard]] NSCursor* GetNativeMouseCursor(ImGuiMouseCursor cursor)
        {
            switch (cursor)
            {
            case ImGuiMouseCursor_Arrow:
                return [NSCursor arrowCursor];
            case ImGuiMouseCursor_TextInput:
                return [NSCursor IBeamCursor];
            case ImGuiMouseCursor_ResizeAll:
                return [NSCursor closedHandCursor];
            case ImGuiMouseCursor_ResizeNS:
                return [NSCursor resizeUpDownCursor];
            case ImGuiMouseCursor_ResizeEW:
                return [NSCursor resizeLeftRightCursor];
            case ImGuiMouseCursor_Hand:
                return [NSCursor pointingHandCursor];
            case ImGuiMouseCursor_NotAllowed:
                return [NSCursor operationNotAllowedCursor];
            case ImGuiMouseCursor_ResizeNESW:
            case ImGuiMouseCursor_ResizeNWSE:
            case ImGuiMouseCursor_Wait:
            case ImGuiMouseCursor_Progress:
            case ImGuiMouseCursor_None:
            case ImGuiMouseCursor_COUNT:
                break;
            }

            return [NSCursor arrowCursor];
        }

        [[nodiscard]] double GetSeconds() noexcept
        {
            using Clock = std::chrono::steady_clock;
            return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
        }

        void SubmitModifierEvents(ImGuiIO& io, InputModifierFlags modifiers)
        {
            io.AddKeyEvent(ImGuiMod_Ctrl, HasInputModifier(modifiers, InputModifierFlags::Control));
            io.AddKeyEvent(ImGuiMod_Shift, HasInputModifier(modifiers, InputModifierFlags::Shift));
            io.AddKeyEvent(ImGuiMod_Alt, HasInputModifier(modifiers, InputModifierFlags::Alt));
            io.AddKeyEvent(ImGuiMod_Super, HasInputModifier(modifiers, InputModifierFlags::Super));
        }
    } // namespace

    MacEditorInputBackend::~MacEditorInputBackend()
    {
        Shutdown();
    }

    ErrorCode MacEditorInputBackend::Init(void* nativeWindowHandle)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        if (nativeWindowHandle == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformName = "VEngine_MacEditorInput";
        io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;
        nativeView_ = nativeWindowHandle;
        lastFrameTimeSeconds_ = GetSeconds();
        initialized_ = true;
        return ErrorCode::None;
    }

    void MacEditorInputBackend::StartFrame()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_)
        {
            return;
        }

        NSView* view = static_cast<NSView*>(nativeView_);
        if (view != nil)
        {
            ImGuiIO& io = ImGui::GetIO();
            const CGFloat backingScale = [[view window] backingScaleFactor];
            io.DisplaySize = ImVec2(static_cast<float>(view.bounds.size.width), static_cast<float>(view.bounds.size.height));
            io.DisplayFramebufferScale = ImVec2(static_cast<float>(backingScale), static_cast<float>(backingScale));

            const double currentTimeSeconds = GetSeconds();
            io.DeltaTime = lastFrameTimeSeconds_ > 0.0 ? static_cast<float>(currentTimeSeconds - lastFrameTimeSeconds_) : 1.0f / 60.0f;
            lastFrameTimeSeconds_ = currentTimeSeconds;

            if ((io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) == 0 && !io.MouseDrawCursor)
            {
                const ImGuiMouseCursor cursor = ImGui::GetMouseCursor();
                if (cursor == ImGuiMouseCursor_None)
                {
                    if (!mouseCursorHidden_)
                    {
                        [NSCursor hide];
                        mouseCursorHidden_ = true;
                    }
                }
                else
                {
                    if (lastMouseCursor_ != cursor)
                    {
                        [GetNativeMouseCursor(cursor) set];
                        lastMouseCursor_ = cursor;
                    }
                    if (mouseCursorHidden_)
                    {
                        [NSCursor unhide];
                        mouseCursorHidden_ = false;
                    }
                }
            }
        }
    }

    void MacEditorInputBackend::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformName = nullptr;
        io.BackendFlags &= ~ImGuiBackendFlags_HasMouseCursors;
        if (mouseCursorHidden_)
        {
            [NSCursor unhide];
            mouseCursorHidden_ = false;
        }
        initialized_ = false;
        nativeView_ = nullptr;
        lastFrameTimeSeconds_ = 0.0;
        lastMouseCursor_ = -1;
        hasMousePosition_ = false;
        mouseDeltaX_ = 0;
        mouseDeltaY_ = 0;
    }

    void MacEditorInputBackend::BeginOSEventFrame() noexcept
    {
        mouseDeltaX_ = 0;
        mouseDeltaY_ = 0;
    }

    bool MacEditorInputBackend::OnOSEvent(const OSEvent& event)
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
        case OSEventType::KeyboardKeyDown:
        case OSEventType::KeyboardKeyUp:
        {
            SubmitModifierEvents(io, event.modifiers);
            const ImGuiKey imguiKey = MacKeyCodeToImGuiKey(event.keyCode);
            if (imguiKey != ImGuiKey_None)
            {
                io.AddKeyEvent(imguiKey, event.type == OSEventType::KeyboardKeyDown);
                io.SetKeyEventNativeData(imguiKey, static_cast<int>(event.keyCode), static_cast<int>(event.scanCode));
            }
            return false;
        }
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

    void MacEditorInputBackend::StoreMousePosition(const OSEvent& event) noexcept
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

    bool MacEditorInputBackend::IsInitialized() const noexcept
    {
        return initialized_;
    }

    Int32 MacEditorInputBackend::GetMouseDeltaX() const noexcept
    {
        return mouseDeltaX_;
    }

    Int32 MacEditorInputBackend::GetMouseDeltaY() const noexcept
    {
        return mouseDeltaY_;
    }

    std::unique_ptr<EditorInputBackend> CreateMacEditorInputBackend()
    {
        return std::make_unique<MacEditorInputBackend>();
    }
} // namespace ve::editor
