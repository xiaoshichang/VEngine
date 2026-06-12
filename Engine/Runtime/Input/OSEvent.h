#pragma once

#include "Engine/Runtime/Core/EnumFlags.h"
#include "Engine/Runtime/Core/Types.h"

namespace ve
{
    enum class OSEventType
    {
        WindowFocusGained,
        WindowFocusLost,
        WindowMinimized,
        WindowRestored,
        WindowResized,
        WindowShown,
        WindowHidden,
        KeyboardKeyDown,
        KeyboardKeyUp,
        TextInput,
        MouseMoved,
        MouseButtonDown,
        MouseButtonUp,
        MouseWheel,
        FrameEndFenceSignal,
    };

    enum class InputMouseButton : UInt8
    {
        Left,
        Right,
        Middle,
        X1,
        X2,
        Count,
    };

    enum class InputModifierFlags : UInt32
    {
        None = 0,
        Shift = 1u << 0u,
        Control = 1u << 1u,
        Alt = 1u << 2u,
        Super = 1u << 3u,
        CapsLock = 1u << 4u,
        NumLock = 1u << 5u,
    };

    template<>
    struct EnableEnumFlags<InputModifierFlags> : std::true_type
    {
    };

    [[nodiscard]] constexpr bool HasInputModifier(InputModifierFlags value, InputModifierFlags flag) noexcept
    {
        return (ToUnderlying(value & flag) != 0u);
    }

    [[nodiscard]] constexpr bool IsInputOSEventType(OSEventType type) noexcept
    {
        switch (type)
        {
        case OSEventType::KeyboardKeyDown:
        case OSEventType::KeyboardKeyUp:
        case OSEventType::TextInput:
        case OSEventType::MouseMoved:
        case OSEventType::MouseButtonDown:
        case OSEventType::MouseButtonUp:
        case OSEventType::MouseWheel:
            return true;
        case OSEventType::WindowFocusGained:
        case OSEventType::WindowFocusLost:
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

    struct OSEvent
    {
        OSEventType type = OSEventType::WindowShown;
        UInt32 width = 0;
        UInt32 height = 0;
        UInt32 fenceIndex = 0;

        UInt32 keyCode = 0;
        UInt32 scanCode = 0;
        InputModifierFlags modifiers = InputModifierFlags::None;
        bool isRepeat = false;
        bool isExtended = false;

        UInt32 textCodepoint = 0;

        Int32 mouseX = 0;
        Int32 mouseY = 0;
        InputMouseButton mouseButton = InputMouseButton::Left;
        Float32 mouseWheelX = 0.0f;
        Float32 mouseWheelY = 0.0f;
    };
} // namespace ve
