#include "Editor/Core/EditorInput.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <imgui.h>
#include <backends/imgui_impl_osx.h>

#import <AppKit/AppKit.h>

namespace ve::editor
{
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

        if (!ImGui_ImplOSX_Init(static_cast<NSView*>(nativeWindowHandle)))
        {
            return ErrorCode::PlatformError;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void EditorInput::StartFrame()
    {
        VE_ASSERT_SCENE_THREAD();
        if (!initialized_)
        {
            return;
        }

        ImGui_ImplOSX_NewFrame(nullptr);
    }

    void EditorInput::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

        ImGui_ImplOSX_Shutdown();
        initialized_ = false;
        hasMousePosition_ = false;
        mouseDeltaX_ = 0;
        mouseDeltaY_ = 0;
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
            io.AddMouseButtonEvent(event.mouseButton == InputMouseButton::Left ? 0 : event.mouseButton == InputMouseButton::Right ? 1 : event.mouseButton == InputMouseButton::Middle ? 2 : event.mouseButton == InputMouseButton::X1 ? 3 : 4,
                                   event.type == OSEventType::MouseButtonDown);
            return false;
        case OSEventType::MouseWheel:
            StoreMousePosition(event);
            io.AddMousePosEvent(static_cast<float>(event.mouseX), static_cast<float>(event.mouseY));
            io.AddMouseWheelEvent(event.mouseWheelX, event.mouseWheelY);
            return false;
        case OSEventType::KeyboardKeyDown:
        case OSEventType::KeyboardKeyUp:
            io.AddKeyEvent(ImGuiMod_Ctrl, HasInputModifier(event.modifiers, InputModifierFlags::Control));
            io.AddKeyEvent(ImGuiMod_Shift, HasInputModifier(event.modifiers, InputModifierFlags::Shift));
            io.AddKeyEvent(ImGuiMod_Alt, HasInputModifier(event.modifiers, InputModifierFlags::Alt));
            io.AddKeyEvent(ImGuiMod_Super, HasInputModifier(event.modifiers, InputModifierFlags::Super));
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
