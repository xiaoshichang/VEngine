#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
    /// Camera data used to build view and projection state for rendering.
    class CameraComponent final : public Component
    {
    public:
        CameraComponent(Scene& scene, GameObject& owner) noexcept;

        enum class ProjectionMode
        {
            Perspective,
            Orthographic,
        };

        [[nodiscard]] bool IsPrimary() const noexcept;
        void SetPrimary(bool primary) noexcept;

        [[nodiscard]] ProjectionMode GetProjectionMode() const noexcept;
        void SetProjectionMode(ProjectionMode mode) noexcept;

        [[nodiscard]] Float32 GetVerticalFieldOfViewRadians() const noexcept;
        void SetVerticalFieldOfViewRadians(Float32 fieldOfViewRadians) noexcept;

        [[nodiscard]] Float32 GetOrthographicSize() const noexcept;
        void SetOrthographicSize(Float32 orthographicSize) noexcept;

        [[nodiscard]] Float32 GetAspectRatio() const noexcept;
        void SetAspectRatio(Float32 aspectRatio) noexcept;

        [[nodiscard]] Float32 GetNearClipPlane() const noexcept;
        void SetNearClipPlane(Float32 nearClipPlane) noexcept;

        [[nodiscard]] Float32 GetFarClipPlane() const noexcept;
        void SetFarClipPlane(Float32 farClipPlane) noexcept;

    private:
        bool primary_ = false;
        ProjectionMode projectionMode_ = ProjectionMode::Perspective;
        Float32 verticalFieldOfViewRadians_ = 1.0471975512f;
        Float32 orthographicSize_ = 5.0f;
        Float32 aspectRatio_ = 1.7777778f;
        Float32 nearClipPlane_ = 0.1f;
        Float32 farClipPlane_ = 1000.0f;
    };
} // namespace ve
