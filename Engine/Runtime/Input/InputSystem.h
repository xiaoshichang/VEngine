#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Input/OSEvent.h"

#include <memory>

namespace ve
{
    struct InputSystemImpl;

    struct InputSystemInitParam
    {
    };

    /// Owns frame-local keyboard and mouse state derived from OS input events.
    ///
    /// InputSystem is updated on the Scene Thread from events produced by the platform layer on the Main Thread.
    /// Platform-specific key codes are stored as UInt32 values for now; later input mapping can translate them into
    /// engine-owned action and axis bindings without changing the OS event route.
    class InputSystem : public NonMovable
    {
    public:
        InputSystem();
        ~InputSystem();

        [[nodiscard]] ErrorCode Initialize(const InputSystemInitParam& desc);
        void Shutdown() noexcept;
        [[nodiscard]] bool IsInitialized() const noexcept;

        void BeginFrame() noexcept;
        void ProcessOSEvent(const OSEvent& event) noexcept;

        [[nodiscard]] bool IsKeyDown(UInt32 keyCode) const noexcept;
        [[nodiscard]] bool WasKeyPressed(UInt32 keyCode) const noexcept;
        [[nodiscard]] bool WasKeyReleased(UInt32 keyCode) const noexcept;

        [[nodiscard]] bool IsMouseButtonDown(InputMouseButton button) const noexcept;
        [[nodiscard]] bool WasMouseButtonPressed(InputMouseButton button) const noexcept;
        [[nodiscard]] bool WasMouseButtonReleased(InputMouseButton button) const noexcept;

        [[nodiscard]] Int32 GetMouseX() const noexcept;
        [[nodiscard]] Int32 GetMouseY() const noexcept;
        [[nodiscard]] Int32 GetMouseDeltaX() const noexcept;
        [[nodiscard]] Int32 GetMouseDeltaY() const noexcept;
        [[nodiscard]] Float32 GetMouseWheelX() const noexcept;
        [[nodiscard]] Float32 GetMouseWheelY() const noexcept;

    private:
        std::unique_ptr<InputSystemImpl> impl_;
    };
} // namespace ve
