#include "Engine/Runtime/Scene/CameraComponent.h"

namespace ve
{
    CameraComponent::CameraComponent(Scene& scene, GameObject& owner) noexcept
        : Component(scene, owner)
    {
    }

    bool CameraComponent::IsPrimary() const noexcept
    {
        return primary_;
    }

    void CameraComponent::SetPrimary(bool primary) noexcept
    {
        primary_ = primary;
    }

    CameraComponent::ProjectionMode CameraComponent::GetProjectionMode() const noexcept
    {
        return projectionMode_;
    }

    void CameraComponent::SetProjectionMode(ProjectionMode mode) noexcept
    {
        projectionMode_ = mode;
    }

    Float32 CameraComponent::GetVerticalFieldOfViewRadians() const noexcept
    {
        return verticalFieldOfViewRadians_;
    }

    void CameraComponent::SetVerticalFieldOfViewRadians(Float32 fieldOfViewRadians) noexcept
    {
        verticalFieldOfViewRadians_ = fieldOfViewRadians;
    }

    Float32 CameraComponent::GetOrthographicSize() const noexcept
    {
        return orthographicSize_;
    }

    void CameraComponent::SetOrthographicSize(Float32 orthographicSize) noexcept
    {
        orthographicSize_ = orthographicSize;
    }

    Float32 CameraComponent::GetAspectRatio() const noexcept
    {
        return aspectRatio_;
    }

    void CameraComponent::SetAspectRatio(Float32 aspectRatio) noexcept
    {
        aspectRatio_ = aspectRatio;
    }

    Float32 CameraComponent::GetNearClipPlane() const noexcept
    {
        return nearClipPlane_;
    }

    void CameraComponent::SetNearClipPlane(Float32 nearClipPlane) noexcept
    {
        nearClipPlane_ = nearClipPlane;
    }

    Float32 CameraComponent::GetFarClipPlane() const noexcept
    {
        return farClipPlane_;
    }

    void CameraComponent::SetFarClipPlane(Float32 farClipPlane) noexcept
    {
        farClipPlane_ = farClipPlane;
    }

} // namespace ve
