#include "Engine/Runtime/Scene/LightComponent.h"

namespace ve
{
    LightType LightComponent::GetLightType() const noexcept
    {
        return type_;
    }

    void LightComponent::SetLightType(LightType type) noexcept
    {
        type_ = type;
    }

    const Vector3& LightComponent::GetColor() const noexcept
    {
        return color_;
    }

    void LightComponent::SetColor(const Vector3& color) noexcept
    {
        color_ = color;
    }

    Float32 LightComponent::GetIntensity() const noexcept
    {
        return intensity_;
    }

    void LightComponent::SetIntensity(Float32 intensity) noexcept
    {
        intensity_ = intensity;
    }
} // namespace ve
