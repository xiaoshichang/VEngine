#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
    /// Camera data used to build view and projection state for rendering.
    class CameraComponent final : public Component
    {
    public:
        [[nodiscard]] bool IsPrimary() const noexcept;
        void SetPrimary(bool primary) noexcept;

        [[nodiscard]] Float32 GetVerticalFieldOfViewRadians() const noexcept;
        void SetVerticalFieldOfViewRadians(Float32 fieldOfViewRadians) noexcept;

        [[nodiscard]] Float32 GetNearClipPlane() const noexcept;
        void SetNearClipPlane(Float32 nearClipPlane) noexcept;

        [[nodiscard]] Float32 GetFarClipPlane() const noexcept;
        void SetFarClipPlane(Float32 farClipPlane) noexcept;

    private:
        bool primary_ = false;
        Float32 verticalFieldOfViewRadians_ = 1.0471975512f;
        Float32 nearClipPlane_ = 0.1f;
        Float32 farClipPlane_ = 1000.0f;
    };
} // namespace ve
