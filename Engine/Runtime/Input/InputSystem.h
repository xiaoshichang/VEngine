#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector2.h"

#include <array>

namespace ve
{
    enum class KeyCode : UInt16
    {
        Unknown = 0,
        A,
        B,
        C,
        D,
        E,
        F,
        G,
        H,
        I,
        J,
        K,
        L,
        M,
        N,
        O,
        P,
        Q,
        R,
        S,
        T,
        U,
        V,
        W,
        X,
        Y,
        Z,
        Num0,
        Num1,
        Num2,
        Num3,
        Num4,
        Num5,
        Num6,
        Num7,
        Num8,
        Num9,
        Space,
        Enter,
        Escape,
        Tab,
        Backspace,
        Left,
        Right,
        Up,
        Down,
        LeftShift,
        RightShift,
        LeftControl,
        RightControl,
        LeftAlt,
        RightAlt,
        Count,
    };

    enum class MouseButton : UInt8
    {
        Left = 0,
        Right,
        Middle,
        X1,
        X2,
        Count,
    };

    struct InputSnapshot
    {
        std::array<bool, static_cast<SizeT>(KeyCode::Count)> keysDown{};
        std::array<bool, static_cast<SizeT>(KeyCode::Count)> keysPressed{};
        std::array<bool, static_cast<SizeT>(KeyCode::Count)> keysReleased{};
        std::array<bool, static_cast<SizeT>(MouseButton::Count)> mouseButtonsDown{};
        std::array<bool, static_cast<SizeT>(MouseButton::Count)> mouseButtonsPressed{};
        std::array<bool, static_cast<SizeT>(MouseButton::Count)> mouseButtonsReleased{};
        Vector2 mousePosition = Vector2::Zero();
        Vector2 mouseDelta = Vector2::Zero();
        Float32 scrollDelta = 0.0f;
        UInt32 viewportWidth = 1;
        UInt32 viewportHeight = 1;
        bool hasFocus = false;
    };

    class InputSystem : public NonMovable
    {
    public:
        [[nodiscard]] static InputSnapshot MergeSnapshots(const InputSnapshot& pending,
                                                          const InputSnapshot& next) noexcept;

        void BeginMainFrame() noexcept;
        void SetFocused(bool focused) noexcept;
        void SetViewportSize(UInt32 width, UInt32 height) noexcept;
        void SubmitKey(KeyCode key, bool pressed) noexcept;
        void SubmitMouseButton(MouseButton button, bool pressed) noexcept;
        void SubmitMouseButtonState(MouseButton button, bool pressed, bool pressedThisFrame, bool releasedThisFrame)
            noexcept;
        void SubmitMousePosition(Float32 x, Float32 y) noexcept;
        void SubmitMouseWheel(Float32 delta) noexcept;
        void ClearMainState() noexcept;

        [[nodiscard]] InputSnapshot CreateGameSnapshot() const noexcept;
        void ApplyGameSnapshot(const InputSnapshot& snapshot) noexcept;
        void ClearGameTransientState() noexcept;

        [[nodiscard]] bool GetKey(KeyCode key) const noexcept;
        [[nodiscard]] bool GetKeyDown(KeyCode key) const noexcept;
        [[nodiscard]] bool GetKeyUp(KeyCode key) const noexcept;
        [[nodiscard]] bool GetMouseButton(MouseButton button) const noexcept;
        [[nodiscard]] bool GetMouseButtonDown(MouseButton button) const noexcept;
        [[nodiscard]] bool GetMouseButtonUp(MouseButton button) const noexcept;
        [[nodiscard]] const Vector2& GetMousePosition() const noexcept;
        [[nodiscard]] const Vector2& GetMouseDelta() const noexcept;
        [[nodiscard]] Float32 GetScrollDelta() const noexcept;
        [[nodiscard]] UInt32 GetViewportWidth() const noexcept;
        [[nodiscard]] UInt32 GetViewportHeight() const noexcept;
        [[nodiscard]] bool HasFocus() const noexcept;

    private:
        [[nodiscard]] static SizeT ToKeyIndex(KeyCode key) noexcept;
        [[nodiscard]] static SizeT ToMouseButtonIndex(MouseButton button) noexcept;

        InputSnapshot mainState_;
        InputSnapshot previousMainState_;
        InputSnapshot gameState_;
    };
} // namespace ve
