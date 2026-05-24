#include "Engine/Runtime/Scene/RenderComponents.h"

#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <cmath>

namespace ve
{
    namespace
    {
        [[nodiscard]] Matrix44
        Perspective(Float32 fovYRadians, Float32 aspectRatio, Float32 nearPlane, Float32 farPlane)
        {
            const Float32 f = 1.0f / std::tan(fovYRadians * 0.5f);
            Matrix44 matrix = Matrix44::Zero();
            matrix.Set(0, 0, f / aspectRatio);
            matrix.Set(1, 1, f);
            matrix.Set(2, 2, farPlane / (farPlane - nearPlane));
            matrix.Set(2, 3, (-nearPlane * farPlane) / (farPlane - nearPlane));
            matrix.Set(3, 2, 1.0f);
            return matrix;
        }

        [[nodiscard]] Matrix44 Orthographic(Float32 size, Float32 aspectRatio, Float32 nearPlane, Float32 farPlane)
        {
            const Float32 height = size;
            const Float32 width = size * aspectRatio;
            Matrix44 matrix = Matrix44::Identity();
            matrix.Set(0, 0, 2.0f / width);
            matrix.Set(1, 1, 2.0f / height);
            matrix.Set(2, 2, 1.0f / (farPlane - nearPlane));
            matrix.Set(2, 3, -nearPlane / (farPlane - nearPlane));
            return matrix;
        }
    } // namespace

    CameraProjectionMode CameraComponent::GetProjectionMode() const noexcept
    {
        return projectionMode_;
    }

    void CameraComponent::SetProjectionMode(CameraProjectionMode mode)
    {
        GetScene().ValidateMutationAccess();
        projectionMode_ = mode;
    }

    Float32 CameraComponent::GetFieldOfViewRadians() const noexcept
    {
        return fieldOfViewRadians_;
    }

    void CameraComponent::SetFieldOfViewRadians(Float32 radians)
    {
        GetScene().ValidateMutationAccess();
        fieldOfViewRadians_ = radians;
    }

    Float32 CameraComponent::GetOrthographicSize() const noexcept
    {
        return orthographicSize_;
    }

    void CameraComponent::SetOrthographicSize(Float32 size)
    {
        GetScene().ValidateMutationAccess();
        orthographicSize_ = size;
    }

    Float32 CameraComponent::GetNearPlane() const noexcept
    {
        return nearPlane_;
    }

    void CameraComponent::SetNearPlane(Float32 nearPlane)
    {
        GetScene().ValidateMutationAccess();
        nearPlane_ = nearPlane;
    }

    Float32 CameraComponent::GetFarPlane() const noexcept
    {
        return farPlane_;
    }

    void CameraComponent::SetFarPlane(Float32 farPlane)
    {
        GetScene().ValidateMutationAccess();
        farPlane_ = farPlane;
    }

    const Vector4& CameraComponent::GetViewportRect() const noexcept
    {
        return viewportRect_;
    }

    void CameraComponent::SetViewportRect(const Vector4& viewportRect)
    {
        GetScene().ValidateMutationAccess();
        viewportRect_ = viewportRect;
    }

    const Vector4& CameraComponent::GetClearColor() const noexcept
    {
        return clearColor_;
    }

    void CameraComponent::SetClearColor(const Vector4& clearColor)
    {
        GetScene().ValidateMutationAccess();
        clearColor_ = clearColor;
    }

    Matrix44 CameraComponent::GetViewMatrix() const noexcept
    {
        const TransformComponent* transform = GetGameObject().GetComponent<TransformComponent>();
        if (transform == nullptr)
        {
            return Matrix44::Identity();
        }

        const Vector3 position = transform->GetWorldPosition();
        const Matrix44 rotation = transform->GetLocalRotation().Conjugated().ToMatrix44();
        return rotation * Matrix44::Translation(-position);
    }

    Matrix44 CameraComponent::GetProjectionMatrix(Float32 aspectRatio) const noexcept
    {
        return projectionMode_ == CameraProjectionMode::Perspective
                   ? Perspective(fieldOfViewRadians_, aspectRatio, nearPlane_, farPlane_)
                   : Orthographic(orthographicSize_, aspectRatio, nearPlane_, farPlane_);
    }

    ResourceHandle<MeshResource> MeshRendererComponent::GetMesh() const noexcept
    {
        return mesh_;
    }

    void MeshRendererComponent::SetMesh(ResourceHandle<MeshResource> mesh)
    {
        GetScene().ValidateMutationAccess();
        mesh_ = mesh;
    }

    ResourceHandle<MaterialResource> MeshRendererComponent::GetMaterial() const noexcept
    {
        return material_;
    }

    void MeshRendererComponent::SetMaterial(ResourceHandle<MaterialResource> material)
    {
        GetScene().ValidateMutationAccess();
        material_ = material;
    }

    bool MeshRendererComponent::IsVisible() const noexcept
    {
        return visible_;
    }

    void MeshRendererComponent::SetVisible(bool visible)
    {
        GetScene().ValidateMutationAccess();
        visible_ = visible;
    }

    const Vector3& MeshRendererComponent::GetLocalBoundsCenter() const noexcept
    {
        return localBoundsCenter_;
    }

    const Vector3& MeshRendererComponent::GetLocalBoundsExtents() const noexcept
    {
        return localBoundsExtents_;
    }

    void MeshRendererComponent::SetLocalBounds(const Vector3& center, const Vector3& extents)
    {
        GetScene().ValidateMutationAccess();
        localBoundsCenter_ = center;
        localBoundsExtents_ = extents;
    }

    LightType LightComponent::GetLightType() const noexcept
    {
        return lightType_;
    }

    void LightComponent::SetLightType(LightType lightType)
    {
        GetScene().ValidateMutationAccess();
        lightType_ = lightType;
    }

    const Vector3& LightComponent::GetColor() const noexcept
    {
        return color_;
    }

    void LightComponent::SetColor(const Vector3& color)
    {
        GetScene().ValidateMutationAccess();
        color_ = color;
    }

    Float32 LightComponent::GetIntensity() const noexcept
    {
        return intensity_;
    }

    void LightComponent::SetIntensity(Float32 intensity)
    {
        GetScene().ValidateMutationAccess();
        intensity_ = intensity;
    }

    Vector3 LightComponent::GetDirection() const noexcept
    {
        const TransformComponent* transform = GetGameObject().GetComponent<TransformComponent>();
        return transform != nullptr ? transform->GetForward().Normalized() : Vector3(0.0f, -1.0f, 0.0f);
    }
} // namespace ve
