#include "Engine/Runtime/Scene/ColliderComponent.h"

namespace ve
{
    bool ColliderBackend::IsRuntimeShapeDirty() const noexcept
    {
        return runtimeShapeDirty_;
    }

    void ColliderBackend::MarkRuntimeShapeDirty() noexcept
    {
        runtimeShapeDirty_ = true;
    }

    void ColliderBackend::ClearRuntimeShapeDirty() noexcept
    {
        runtimeShapeDirty_ = false;
    }

    ColliderComponent::ColliderComponent(Scene& scene, GameObject& owner) noexcept
        : Component(scene, owner)
    {
    }

    const ColliderDesc& ColliderComponent::GetDesc() const noexcept
    {
        return desc_;
    }

    void ColliderComponent::SetDesc(const ColliderDesc& desc) noexcept
    {
        desc_ = desc;
        MarkBackendDirty();
    }

    ColliderBackend& ColliderComponent::GetBackend() noexcept
    {
        return backend_;
    }

    const ColliderBackend& ColliderComponent::GetBackend() const noexcept
    {
        return backend_;
    }

    ColliderShapeType ColliderComponent::GetShapeType() const noexcept
    {
        return desc_.shapeType;
    }

    void ColliderComponent::SetShapeType(ColliderShapeType shapeType) noexcept
    {
        desc_.shapeType = shapeType;
        MarkBackendDirty();
    }

    bool ColliderComponent::IsTrigger() const noexcept
    {
        return desc_.trigger;
    }

    void ColliderComponent::SetTrigger(bool trigger) noexcept
    {
        desc_.trigger = trigger;
        MarkBackendDirty();
    }

    const Vector3& ColliderComponent::GetCenter() const noexcept
    {
        return desc_.center;
    }

    void ColliderComponent::SetCenter(const Vector3& center) noexcept
    {
        desc_.center = center;
        MarkBackendDirty();
    }

    const Vector3& ColliderComponent::GetSize() const noexcept
    {
        return desc_.size;
    }

    void ColliderComponent::SetSize(const Vector3& size) noexcept
    {
        desc_.size = size;
        MarkBackendDirty();
    }

    Float32 ColliderComponent::GetRadius() const noexcept
    {
        return desc_.radius;
    }

    void ColliderComponent::SetRadius(Float32 radius) noexcept
    {
        desc_.radius = radius;
        MarkBackendDirty();
    }

    Float32 ColliderComponent::GetHeight() const noexcept
    {
        return desc_.height;
    }

    void ColliderComponent::SetHeight(Float32 height) noexcept
    {
        desc_.height = height;
        MarkBackendDirty();
    }

    ColliderDirectionAxis ColliderComponent::GetDirection() const noexcept
    {
        return desc_.direction;
    }

    void ColliderComponent::SetDirection(ColliderDirectionAxis direction) noexcept
    {
        desc_.direction = direction;
        MarkBackendDirty();
    }

    Float32 ColliderComponent::GetStaticFriction() const noexcept
    {
        return desc_.staticFriction;
    }

    void ColliderComponent::SetStaticFriction(Float32 staticFriction) noexcept
    {
        desc_.staticFriction = staticFriction;
        MarkBackendDirty();
    }

    Float32 ColliderComponent::GetDynamicFriction() const noexcept
    {
        return desc_.dynamicFriction;
    }

    void ColliderComponent::SetDynamicFriction(Float32 dynamicFriction) noexcept
    {
        desc_.dynamicFriction = dynamicFriction;
        MarkBackendDirty();
    }

    Float32 ColliderComponent::GetBounciness() const noexcept
    {
        return desc_.bounciness;
    }

    void ColliderComponent::SetBounciness(Float32 bounciness) noexcept
    {
        desc_.bounciness = bounciness;
        MarkBackendDirty();
    }

    void ColliderComponent::MarkBackendDirty() noexcept
    {
        backend_.MarkRuntimeShapeDirty();
    }
} // namespace ve
