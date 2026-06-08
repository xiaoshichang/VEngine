#include "Engine/Runtime/Scene/Component.h"

#include <utility>

namespace ve
{
    GameObject* Component::GetOwner() noexcept
    {
        return owner_;
    }

    const GameObject* Component::GetOwner() const noexcept
    {
        return owner_;
    }

    bool Component::IsEnabled() const noexcept
    {
        return enabled_;
    }

    void Component::SetEnabled(bool enabled) noexcept
    {
        enabled_ = enabled;
    }

    void Component::OnUpdate(Float32 deltaSeconds)
    {
        static_cast<void>(deltaSeconds);
    }

    void Component::SetOwner(GameObject* owner) noexcept
    {
        owner_ = owner;
    }

    const Vector3& TransformComponent::GetLocalPosition() const noexcept
    {
        return localPosition_;
    }

    void TransformComponent::SetLocalPosition(const Vector3& position) noexcept
    {
        localPosition_ = position;
    }

    const Quaternion& TransformComponent::GetLocalRotation() const noexcept
    {
        return localRotation_;
    }

    void TransformComponent::SetLocalRotation(const Quaternion& rotation) noexcept
    {
        localRotation_ = rotation.Normalized();
    }

    const Vector3& TransformComponent::GetLocalScale() const noexcept
    {
        return localScale_;
    }

    void TransformComponent::SetLocalScale(const Vector3& scale) noexcept
    {
        localScale_ = scale;
    }

    Matrix44 TransformComponent::GetLocalMatrix() const noexcept
    {
        return Matrix44::Translation(localPosition_) * localRotation_.ToMatrix44() * Matrix44::Scale(localScale_);
    }

    const std::string& MeshRenderComponent::GetMeshAssetPath() const noexcept
    {
        return meshAssetPath_;
    }

    void MeshRenderComponent::SetMeshAssetPath(std::string meshAssetPath)
    {
        meshAssetPath_ = std::move(meshAssetPath);
    }

    const std::string& MeshRenderComponent::GetMaterialAssetPath() const noexcept
    {
        return materialAssetPath_;
    }

    void MeshRenderComponent::SetMaterialAssetPath(std::string materialAssetPath)
    {
        materialAssetPath_ = std::move(materialAssetPath);
    }

    bool MeshRenderComponent::IsVisible() const noexcept
    {
        return visible_;
    }

    void MeshRenderComponent::SetVisible(bool visible) noexcept
    {
        visible_ = visible;
    }

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
