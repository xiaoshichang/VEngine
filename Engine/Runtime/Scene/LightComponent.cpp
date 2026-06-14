#include "Engine/Runtime/Scene/LightComponent.h"

namespace ve
{
    LightComponent::LightComponent(Scene& scene, GameObject& owner) noexcept
        : Component(scene, owner)
    {
    }

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

    const Vector3& LightComponent::GetDirection() const noexcept
    {
        return direction_;
    }

    void LightComponent::SetDirection(const Vector3& direction) noexcept
    {
        direction_ = direction.Normalized();
    }

    Float32 LightComponent::GetIntensity() const noexcept
    {
        return intensity_;
    }

    void LightComponent::SetIntensity(Float32 intensity) noexcept
    {
        intensity_ = intensity;
    }

    Float32 LightComponent::GetRange() const noexcept
    {
        return range_;
    }

    void LightComponent::SetRange(Float32 range) noexcept
    {
        range_ = range;
    }

    Float32 LightComponent::GetInnerConeAngleRadians() const noexcept
    {
        return innerConeAngleRadians_;
    }

    void LightComponent::SetInnerConeAngleRadians(Float32 innerConeAngleRadians) noexcept
    {
        innerConeAngleRadians_ = innerConeAngleRadians;
    }

    Float32 LightComponent::GetOuterConeAngleRadians() const noexcept
    {
        return outerConeAngleRadians_;
    }

    void LightComponent::SetOuterConeAngleRadians(Float32 outerConeAngleRadians) noexcept
    {
        outerConeAngleRadians_ = outerConeAngleRadians;
    }

    bool LightComponent::CastShadows() const noexcept
    {
        return castShadows_;
    }

    void LightComponent::SetCastShadows(bool castShadows) noexcept
    {
        castShadows_ = castShadows;
    }

} // namespace ve
