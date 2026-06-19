#include "Engine/Runtime/Input/InputSystem.h"

#include "Engine/Runtime/Logging/Log.h"

#include <algorithm>
#include <array>

namespace ve
{
    namespace
    {
        constexpr SizeT KeyStateCount = 256;
        constexpr SizeT MouseButtonCount = static_cast<SizeT>(InputMouseButton::Count);

        [[nodiscard]] constexpr bool IsTrackedKeyCode(UInt32 keyCode) noexcept
        {
            return keyCode < KeyStateCount;
        }

        [[nodiscard]] constexpr bool IsTrackedMouseButton(InputMouseButton button) noexcept
        {
            return static_cast<SizeT>(button) < MouseButtonCount;
        }
    } // namespace

    struct InputSystemImpl
    {
        std::array<bool, KeyStateCount> keysDown = {};
        std::array<bool, KeyStateCount> keysPressed = {};
        std::array<bool, KeyStateCount> keysReleased = {};

        std::array<bool, MouseButtonCount> mouseButtonsDown = {};
        std::array<bool, MouseButtonCount> mouseButtonsPressed = {};
        std::array<bool, MouseButtonCount> mouseButtonsReleased = {};

        Int32 mouseX = 0;
        Int32 mouseY = 0;
        Int32 mouseDeltaX = 0;
        Int32 mouseDeltaY = 0;
        Float32 mouseWheelX = 0.0f;
        Float32 mouseWheelY = 0.0f;
        bool hasMousePosition = false;
        bool initialized = false;
    };

    namespace
    {
        void ResetDownState(InputSystemImpl& impl) noexcept
        {
            std::fill(impl.keysDown.begin(), impl.keysDown.end(), false);
            std::fill(impl.mouseButtonsDown.begin(), impl.mouseButtonsDown.end(), false);
        }

        void StoreMousePosition(InputSystemImpl& impl, const OSEvent& event) noexcept
        {
            if (impl.hasMousePosition)
            {
                impl.mouseDeltaX += event.mouseX - impl.mouseX;
                impl.mouseDeltaY += event.mouseY - impl.mouseY;
            }

            impl.mouseX = event.mouseX;
            impl.mouseY = event.mouseY;
            impl.hasMousePosition = true;
        }
    } // namespace

    InputSystem::InputSystem()
        : impl_(std::make_unique<InputSystemImpl>())
    {
    }

    InputSystem::~InputSystem()
    {
        Shutdown();
    }

    ErrorCode InputSystem::Initialize(const InputSystemInitParam& desc)
    {
        (void)desc;
        if (impl_->initialized)
        {
            return ErrorCode::InvalidState;
        }

        impl_->initialized = true;
        return ErrorCode::None;
    }

    void InputSystem::Shutdown() noexcept
    {
        if (!impl_->initialized)
        {
            return;
        }

        BeginFrame();
        ResetDownState(*impl_);
        impl_->hasMousePosition = false;
        impl_->initialized = false;
    }

    bool InputSystem::IsInitialized() const noexcept
    {
        return impl_->initialized;
    }

    void InputSystem::BeginFrame() noexcept
    {
        std::fill(impl_->keysPressed.begin(), impl_->keysPressed.end(), false);
        std::fill(impl_->keysReleased.begin(), impl_->keysReleased.end(), false);
        std::fill(impl_->mouseButtonsPressed.begin(), impl_->mouseButtonsPressed.end(), false);
        std::fill(impl_->mouseButtonsReleased.begin(), impl_->mouseButtonsReleased.end(), false);
        impl_->mouseDeltaX = 0;
        impl_->mouseDeltaY = 0;
        impl_->mouseWheelX = 0.0f;
        impl_->mouseWheelY = 0.0f;
    }

    void InputSystem::ProcessOSEvent(const OSEvent& event) noexcept
    {
        if (!impl_->initialized)
        {
            return;
        }

        switch (event.type)
        {
        case OSEventType::KeyboardKeyDown:
            if (IsTrackedKeyCode(event.keyCode))
            {
                const SizeT keyIndex = event.keyCode;
                if (!impl_->keysDown[keyIndex])
                {
                    impl_->keysPressed[keyIndex] = true;
                    VE_LOG_INFO("Key pressed: {}", event.keyCode);
                }
                impl_->keysDown[keyIndex] = true;
            }
            break;
        case OSEventType::KeyboardKeyUp:
            if (IsTrackedKeyCode(event.keyCode))
            {
                const SizeT keyIndex = event.keyCode;
                if (impl_->keysDown[keyIndex])
                {
                    impl_->keysReleased[keyIndex] = true;
                }
                impl_->keysDown[keyIndex] = false;
            }
            break;
        case OSEventType::MouseMoved:
            StoreMousePosition(*impl_, event);
            break;
        case OSEventType::MouseButtonDown:
            StoreMousePosition(*impl_, event);
            if (IsTrackedMouseButton(event.mouseButton))
            {
                const SizeT buttonIndex = static_cast<SizeT>(event.mouseButton);
                if (!impl_->mouseButtonsDown[buttonIndex])
                {
                    impl_->mouseButtonsPressed[buttonIndex] = true;
                }
                impl_->mouseButtonsDown[buttonIndex] = true;
            }
            break;
        case OSEventType::MouseButtonUp:
            StoreMousePosition(*impl_, event);
            if (IsTrackedMouseButton(event.mouseButton))
            {
                const SizeT buttonIndex = static_cast<SizeT>(event.mouseButton);
                if (impl_->mouseButtonsDown[buttonIndex])
                {
                    impl_->mouseButtonsReleased[buttonIndex] = true;
                }
                impl_->mouseButtonsDown[buttonIndex] = false;
            }
            break;
        case OSEventType::MouseWheel:
            StoreMousePosition(*impl_, event);
            impl_->mouseWheelX += event.mouseWheelX;
            impl_->mouseWheelY += event.mouseWheelY;
            break;
        case OSEventType::WindowFocusLost:
            ResetDownState(*impl_);
            break;
        case OSEventType::WindowFocusGained:
        case OSEventType::WindowMinimized:
        case OSEventType::WindowRestored:
        case OSEventType::WindowResized:
        case OSEventType::WindowShown:
        case OSEventType::WindowHidden:
        case OSEventType::TextInput:
        case OSEventType::FrameEndFenceSignal:
            break;
        }
    }

    bool InputSystem::IsKeyDown(UInt32 keyCode) const noexcept
    {
        return IsTrackedKeyCode(keyCode) && impl_->keysDown[keyCode];
    }

    bool InputSystem::WasKeyPressed(UInt32 keyCode) const noexcept
    {
        return IsTrackedKeyCode(keyCode) && impl_->keysPressed[keyCode];
    }

    bool InputSystem::WasKeyReleased(UInt32 keyCode) const noexcept
    {
        return IsTrackedKeyCode(keyCode) && impl_->keysReleased[keyCode];
    }

    bool InputSystem::IsMouseButtonDown(InputMouseButton button) const noexcept
    {
        return IsTrackedMouseButton(button) && impl_->mouseButtonsDown[static_cast<SizeT>(button)];
    }

    bool InputSystem::WasMouseButtonPressed(InputMouseButton button) const noexcept
    {
        return IsTrackedMouseButton(button) && impl_->mouseButtonsPressed[static_cast<SizeT>(button)];
    }

    bool InputSystem::WasMouseButtonReleased(InputMouseButton button) const noexcept
    {
        return IsTrackedMouseButton(button) && impl_->mouseButtonsReleased[static_cast<SizeT>(button)];
    }

    Int32 InputSystem::GetMouseX() const noexcept
    {
        return impl_->mouseX;
    }

    Int32 InputSystem::GetMouseY() const noexcept
    {
        return impl_->mouseY;
    }

    Int32 InputSystem::GetMouseDeltaX() const noexcept
    {
        return impl_->mouseDeltaX;
    }

    Int32 InputSystem::GetMouseDeltaY() const noexcept
    {
        return impl_->mouseDeltaY;
    }

    Float32 InputSystem::GetMouseWheelX() const noexcept
    {
        return impl_->mouseWheelX;
    }

    Float32 InputSystem::GetMouseWheelY() const noexcept
    {
        return impl_->mouseWheelY;
    }
} // namespace ve
