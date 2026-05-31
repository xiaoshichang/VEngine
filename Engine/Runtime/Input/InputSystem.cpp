#include "Engine/Runtime/Input/InputSystem.h"

#include <algorithm>

namespace ve
{
    namespace
    {
        template<typename T, SizeT Count>
        [[nodiscard]] bool GetArrayValue(const std::array<bool, Count>& values, T value) noexcept
        {
            const SizeT index = static_cast<SizeT>(value);
            return index < Count ? values[index] : false;
        }
    } // namespace

    InputSnapshot InputSystem::MergeSnapshots(const InputSnapshot& pending, const InputSnapshot& next) noexcept
    {
        if (!next.hasFocus)
        {
            return next;
        }

        InputSnapshot merged = next;
        for (SizeT index = 0; index < merged.keysPressed.size(); ++index)
        {
            merged.keysPressed[index] = pending.keysPressed[index] || next.keysPressed[index];
            merged.keysReleased[index] = pending.keysReleased[index] || next.keysReleased[index];
        }

        for (SizeT index = 0; index < merged.mouseButtonsPressed.size(); ++index)
        {
            merged.mouseButtonsPressed[index] =
                pending.mouseButtonsPressed[index] || next.mouseButtonsPressed[index];
            merged.mouseButtonsReleased[index] =
                pending.mouseButtonsReleased[index] || next.mouseButtonsReleased[index];
        }

        merged.mouseDelta = pending.mouseDelta + next.mouseDelta;
        merged.scrollDelta = pending.scrollDelta + next.scrollDelta;
        return merged;
    }

    void InputSystem::BeginMainFrame() noexcept
    {
        previousMainState_.keysDown = mainState_.keysDown;
        previousMainState_.mouseButtonsDown = mainState_.mouseButtonsDown;
        previousMainState_.mousePosition = mainState_.mousePosition;

        mainState_.keysPressed.fill(false);
        mainState_.keysReleased.fill(false);
        mainState_.mouseButtonsPressed.fill(false);
        mainState_.mouseButtonsReleased.fill(false);
        mainState_.mouseDelta = Vector2::Zero();
        mainState_.scrollDelta = 0.0f;
    }

    void InputSystem::SetFocused(bool focused) noexcept
    {
        mainState_.hasFocus = focused;
        if (!focused)
        {
            ClearMainState();
        }
    }

    void InputSystem::SetViewportSize(UInt32 width, UInt32 height) noexcept
    {
        mainState_.viewportWidth = std::max(width, 1u);
        mainState_.viewportHeight = std::max(height, 1u);
    }

    void InputSystem::SubmitKey(KeyCode key, bool pressed) noexcept
    {
        const SizeT index = ToKeyIndex(key);
        if (index == 0 || index >= mainState_.keysDown.size())
        {
            return;
        }

        const bool wasDown = mainState_.keysDown[index];
        mainState_.keysDown[index] = pressed;
        mainState_.keysPressed[index] = mainState_.keysPressed[index] || (pressed && !wasDown);
        mainState_.keysReleased[index] = mainState_.keysReleased[index] || (!pressed && wasDown);
    }

    void InputSystem::SubmitMouseButton(MouseButton button, bool pressed) noexcept
    {
        const SizeT index = ToMouseButtonIndex(button);
        if (index >= mainState_.mouseButtonsDown.size())
        {
            return;
        }

        const bool wasDown = mainState_.mouseButtonsDown[index];
        mainState_.mouseButtonsDown[index] = pressed;
        mainState_.mouseButtonsPressed[index] = mainState_.mouseButtonsPressed[index] || (pressed && !wasDown);
        mainState_.mouseButtonsReleased[index] = mainState_.mouseButtonsReleased[index] || (!pressed && wasDown);
    }

    void InputSystem::SubmitMouseButtonState(MouseButton button,
                                             bool pressed,
                                             bool pressedThisFrame,
                                             bool releasedThisFrame) noexcept
    {
        const SizeT index = ToMouseButtonIndex(button);
        if (index >= mainState_.mouseButtonsDown.size())
        {
            return;
        }

        mainState_.mouseButtonsDown[index] = pressed;
        mainState_.mouseButtonsPressed[index] = mainState_.mouseButtonsPressed[index] || pressedThisFrame;
        mainState_.mouseButtonsReleased[index] = mainState_.mouseButtonsReleased[index] || releasedThisFrame;
    }

    void InputSystem::SubmitMousePosition(Float32 x, Float32 y) noexcept
    {
        const Vector2 nextPosition(x, y);
        mainState_.mouseDelta += nextPosition - mainState_.mousePosition;
        mainState_.mousePosition = nextPosition;
    }

    void InputSystem::SubmitMouseWheel(Float32 delta) noexcept
    {
        mainState_.scrollDelta += delta;
    }

    void InputSystem::ClearMainState() noexcept
    {
        const Vector2 mousePosition = mainState_.mousePosition;
        const UInt32 viewportWidth = mainState_.viewportWidth;
        const UInt32 viewportHeight = mainState_.viewportHeight;
        const bool hasFocus = mainState_.hasFocus;

        mainState_ = InputSnapshot{};
        previousMainState_ = InputSnapshot{};
        mainState_.mousePosition = mousePosition;
        previousMainState_.mousePosition = mousePosition;
        mainState_.viewportWidth = viewportWidth;
        mainState_.viewportHeight = viewportHeight;
        previousMainState_.viewportWidth = viewportWidth;
        previousMainState_.viewportHeight = viewportHeight;
        mainState_.hasFocus = hasFocus;
        previousMainState_.hasFocus = hasFocus;
    }

    InputSnapshot InputSystem::CreateGameSnapshot() const noexcept
    {
        return mainState_;
    }

    void InputSystem::ApplyGameSnapshot(const InputSnapshot& snapshot) noexcept
    {
        gameState_ = snapshot;
    }

    void InputSystem::ClearGameTransientState() noexcept
    {
        gameState_.keysPressed.fill(false);
        gameState_.keysReleased.fill(false);
        gameState_.mouseButtonsPressed.fill(false);
        gameState_.mouseButtonsReleased.fill(false);
        gameState_.mouseDelta = Vector2::Zero();
        gameState_.scrollDelta = 0.0f;
    }

    bool InputSystem::GetKey(KeyCode key) const noexcept
    {
        return GetArrayValue(gameState_.keysDown, key);
    }

    bool InputSystem::GetKeyDown(KeyCode key) const noexcept
    {
        return GetArrayValue(gameState_.keysPressed, key);
    }

    bool InputSystem::GetKeyUp(KeyCode key) const noexcept
    {
        return GetArrayValue(gameState_.keysReleased, key);
    }

    bool InputSystem::GetMouseButton(MouseButton button) const noexcept
    {
        return GetArrayValue(gameState_.mouseButtonsDown, button);
    }

    bool InputSystem::GetMouseButtonDown(MouseButton button) const noexcept
    {
        return GetArrayValue(gameState_.mouseButtonsPressed, button);
    }

    bool InputSystem::GetMouseButtonUp(MouseButton button) const noexcept
    {
        return GetArrayValue(gameState_.mouseButtonsReleased, button);
    }

    const Vector2& InputSystem::GetMousePosition() const noexcept
    {
        return gameState_.mousePosition;
    }

    const Vector2& InputSystem::GetMouseDelta() const noexcept
    {
        return gameState_.mouseDelta;
    }

    Float32 InputSystem::GetScrollDelta() const noexcept
    {
        return gameState_.scrollDelta;
    }

    UInt32 InputSystem::GetViewportWidth() const noexcept
    {
        return gameState_.viewportWidth;
    }

    UInt32 InputSystem::GetViewportHeight() const noexcept
    {
        return gameState_.viewportHeight;
    }

    bool InputSystem::HasFocus() const noexcept
    {
        return gameState_.hasFocus;
    }

    SizeT InputSystem::ToKeyIndex(KeyCode key) noexcept
    {
        return static_cast<SizeT>(key);
    }

    SizeT InputSystem::ToMouseButtonIndex(MouseButton button) noexcept
    {
        return static_cast<SizeT>(button);
    }
} // namespace ve
