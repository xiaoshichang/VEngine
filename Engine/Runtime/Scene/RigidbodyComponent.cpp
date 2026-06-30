#include "Engine/Runtime/Scene/RigidbodyComponent.h"

namespace ve
{
    PhysicsBodyHandle RigidbodyBackend::GetPhysicsBodyHandle() const noexcept
    {
        return bodyHandle_;
    }

    void RigidbodyBackend::SetPhysicsBodyHandle(PhysicsBodyHandle bodyHandle) noexcept
    {
        bodyHandle_ = bodyHandle;
    }

    void RigidbodyBackend::ClearPhysicsBodyHandle() noexcept
    {
        bodyHandle_.Reset();
    }

    bool RigidbodyBackend::IsRuntimeBodyDirty() const noexcept
    {
        return runtimeBodyDirty_;
    }

    void RigidbodyBackend::MarkRuntimeBodyDirty() noexcept
    {
        runtimeBodyDirty_ = true;
    }

    void RigidbodyBackend::ClearRuntimeBodyDirty() noexcept
    {
        runtimeBodyDirty_ = false;
    }

    RigidbodyComponent::RigidbodyComponent(Scene& scene, GameObject& owner) noexcept
        : Component(scene, owner)
    {
    }

    const RigidbodyDesc& RigidbodyComponent::GetDesc() const noexcept
    {
        return desc_;
    }

    void RigidbodyComponent::SetDesc(const RigidbodyDesc& desc) noexcept
    {
        desc_ = desc;
        MarkBackendDirty();
    }

    RigidbodyBackend& RigidbodyComponent::GetBackend() noexcept
    {
        return backend_;
    }

    const RigidbodyBackend& RigidbodyComponent::GetBackend() const noexcept
    {
        return backend_;
    }

    Float32 RigidbodyComponent::GetMass() const noexcept
    {
        return desc_.mass;
    }

    void RigidbodyComponent::SetMass(Float32 mass) noexcept
    {
        desc_.mass = mass;
        MarkBackendDirty();
    }

    Float32 RigidbodyComponent::GetLinearDamping() const noexcept
    {
        return desc_.linearDamping;
    }

    void RigidbodyComponent::SetLinearDamping(Float32 linearDamping) noexcept
    {
        desc_.linearDamping = linearDamping;
        MarkBackendDirty();
    }

    Float32 RigidbodyComponent::GetAngularDamping() const noexcept
    {
        return desc_.angularDamping;
    }

    void RigidbodyComponent::SetAngularDamping(Float32 angularDamping) noexcept
    {
        desc_.angularDamping = angularDamping;
        MarkBackendDirty();
    }

    bool RigidbodyComponent::UsesGravity() const noexcept
    {
        return desc_.useGravity;
    }

    void RigidbodyComponent::SetUseGravity(bool useGravity) noexcept
    {
        desc_.useGravity = useGravity;
        MarkBackendDirty();
    }

    bool RigidbodyComponent::IsKinematic() const noexcept
    {
        return desc_.kinematic;
    }

    void RigidbodyComponent::SetKinematic(bool kinematic) noexcept
    {
        desc_.kinematic = kinematic;
        MarkBackendDirty();
    }

    bool RigidbodyComponent::DetectsCollisions() const noexcept
    {
        return desc_.detectCollisions;
    }

    void RigidbodyComponent::SetDetectCollisions(bool detectCollisions) noexcept
    {
        desc_.detectCollisions = detectCollisions;
        MarkBackendDirty();
    }

    RigidbodyInterpolationMode RigidbodyComponent::GetInterpolationMode() const noexcept
    {
        return desc_.interpolationMode;
    }

    void RigidbodyComponent::SetInterpolationMode(RigidbodyInterpolationMode interpolationMode) noexcept
    {
        desc_.interpolationMode = interpolationMode;
        MarkBackendDirty();
    }

    RigidbodyCollisionDetectionMode RigidbodyComponent::GetCollisionDetectionMode() const noexcept
    {
        return desc_.collisionDetectionMode;
    }

    void RigidbodyComponent::SetCollisionDetectionMode(RigidbodyCollisionDetectionMode collisionDetectionMode) noexcept
    {
        desc_.collisionDetectionMode = collisionDetectionMode;
        MarkBackendDirty();
    }

    RigidbodyConstraintFlags RigidbodyComponent::GetConstraints() const noexcept
    {
        return desc_.constraints;
    }

    void RigidbodyComponent::SetConstraints(RigidbodyConstraintFlags constraints) noexcept
    {
        desc_.constraints = constraints;
        MarkBackendDirty();
    }

    bool RigidbodyComponent::HasConstraint(RigidbodyConstraintFlags constraint) const noexcept
    {
        return (ToUnderlying(desc_.constraints) & ToUnderlying(constraint)) != 0;
    }

    PhysicsBodyHandle RigidbodyComponent::GetPhysicsBodyHandle() const noexcept
    {
        return backend_.GetPhysicsBodyHandle();
    }

    void RigidbodyComponent::SetPhysicsBodyHandle(PhysicsBodyHandle bodyHandle) noexcept
    {
        backend_.SetPhysicsBodyHandle(bodyHandle);
    }

    void RigidbodyComponent::ClearPhysicsBodyHandle() noexcept
    {
        backend_.ClearPhysicsBodyHandle();
    }

    void RigidbodyComponent::MarkBackendDirty() noexcept
    {
        backend_.MarkRuntimeBodyDirty();
    }
} // namespace ve
