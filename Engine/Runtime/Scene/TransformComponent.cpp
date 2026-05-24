#include "Engine/Runtime/Scene/TransformComponent.h"

#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"

namespace ve
{
    const Vector3& TransformComponent::GetLocalPosition() const noexcept
    {
        return localPosition_;
    }

    void TransformComponent::SetLocalPosition(const Vector3& position)
    {
        GetScene().ValidateMutationAccess();
        localPosition_ = position;
        MarkDirty();
    }

    const Quaternion& TransformComponent::GetLocalRotation() const noexcept
    {
        return localRotation_;
    }

    void TransformComponent::SetLocalRotation(const Quaternion& rotation)
    {
        GetScene().ValidateMutationAccess();
        localRotation_ = rotation.Normalized();
        MarkDirty();
    }

    const Vector3& TransformComponent::GetLocalScale() const noexcept
    {
        return localScale_;
    }

    void TransformComponent::SetLocalScale(const Vector3& scale)
    {
        GetScene().ValidateMutationAccess();
        localScale_ = scale;
        MarkDirty();
    }

    const Matrix44& TransformComponent::GetLocalMatrix() const noexcept
    {
        return localMatrix_;
    }

    const Matrix44& TransformComponent::GetWorldMatrix() const noexcept
    {
        return worldMatrix_;
    }

    Vector3 TransformComponent::GetWorldPosition() const noexcept
    {
        return Vector3(worldMatrix_.Get(0, 3), worldMatrix_.Get(1, 3), worldMatrix_.Get(2, 3));
    }

    Vector3 TransformComponent::GetForward() const noexcept
    {
        return worldMatrix_.TransformDirection(Vector3(0.0f, 0.0f, 1.0f)).Normalized();
    }

    bool TransformComponent::IsDirty() const noexcept
    {
        return dirty_;
    }

    void TransformComponent::MarkDirty() noexcept
    {
        dirty_ = true;
        GetGameObject().MarkTransformHierarchyDirty();
    }

    void TransformComponent::UpdateWorldTransform(const Matrix44& parentWorld)
    {
        if (dirty_)
        {
            RebuildLocalMatrix();
        }

        worldMatrix_ = parentWorld * localMatrix_;
        dirty_ = false;
    }

    void TransformComponent::RebuildLocalMatrix()
    {
        localMatrix_ =
            Matrix44::Translation(localPosition_) * localRotation_.ToMatrix44() * Matrix44::Scale(localScale_);
    }
} // namespace ve
