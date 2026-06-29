#include "Editor/macOS/MacEditorInputBackend.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <imgui.h>
#include <backends/imgui_impl_osx.h>

#import <AppKit/AppKit.h>

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

        if (!ImGui_ImplOSX_Init(static_cast<NSView*>(nativeWindowHandle)))
        {
            return ErrorCode::PlatformError;
        }

        nativeView_ = nativeWindowHandle;
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

        ImGui_ImplOSX_NewFrame(static_cast<NSView*>(nativeView_));
    }

    void MacEditorInputBackend::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

        ImGui_ImplOSX_Shutdown();
        initialized_ = false;
        nativeView_ = nullptr;
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
            SubmitModifierEvents(io, event.modifiers);
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
