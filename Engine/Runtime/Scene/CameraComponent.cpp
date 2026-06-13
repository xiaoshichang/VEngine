#include "Engine/Runtime/Scene/CameraComponent.h"

namespace ve
{
    bool CameraComponent::IsPrimary() const noexcept
    {
        return primary_;
    }

    void CameraComponent::SetPrimary(bool primary) noexcept
    {
        primary_ = primary;
    }

    Float32 CameraComponent::GetVerticalFieldOfViewRadians() const noexcept
    {
        return verticalFieldOfViewRadians_;
    }

    void CameraComponent::SetVerticalFieldOfViewRadians(Float32 fieldOfViewRadians) noexcept
    {
        verticalFieldOfViewRadians_ = fieldOfViewRadians;
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
