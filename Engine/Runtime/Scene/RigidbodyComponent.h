#pragma once

#include "Engine/Runtime/Core/EnumFlags.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Physics/PhysicsTypes.h"
#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
    enum class RigidbodyInterpolationMode
    {
        None,
        Interpolate,
        Extrapolate,
    };

    enum class RigidbodyCollisionDetectionMode
    {
        Discrete,
        Continuous,
        ContinuousDynamic,
        ContinuousSpeculative,
    };

    enum class RigidbodyConstraintFlags : UInt32
    {
        None = 0,
        FreezePositionX = 1u << 0,
        FreezePositionY = 1u << 1,
        FreezePositionZ = 1u << 2,
        FreezeRotationX = 1u << 3,
        FreezeRotationY = 1u << 4,
        FreezeRotationZ = 1u << 5,
        FreezePosition = (1u << 0) | (1u << 1) | (1u << 2),
        FreezeRotation = (1u << 3) | (1u << 4) | (1u << 5),
        FreezeAll = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3) | (1u << 4) | (1u << 5),
    };

    template<>
    struct EnableEnumFlags<RigidbodyConstraintFlags> : std::true_type
    {
    };

    struct RigidbodyDesc
    {
        Float32 mass = 1.0f;
        Float32 linearDamping = 0.0f;
        Float32 angularDamping = 0.05f;
        bool useGravity = true;
        bool kinematic = false;
        bool detectCollisions = true;
        RigidbodyInterpolationMode interpolationMode = RigidbodyInterpolationMode::None;
        RigidbodyCollisionDetectionMode collisionDetectionMode = RigidbodyCollisionDetectionMode::Discrete;
        RigidbodyConstraintFlags constraints = RigidbodyConstraintFlags::None;
    };

    class RigidbodyBackend final
    {
    public:
        [[nodiscard]] PhysicsBodyHandle GetPhysicsBodyHandle() const noexcept;
        void SetPhysicsBodyHandle(PhysicsBodyHandle bodyHandle) noexcept;
        void ClearPhysicsBodyHandle() noexcept;

        [[nodiscard]] bool IsRuntimeBodyDirty() const noexcept;
        void MarkRuntimeBodyDirty() noexcept;
        void ClearRuntimeBodyDirty() noexcept;

    private:
        PhysicsBodyHandle bodyHandle_;
        bool runtimeBodyDirty_ = true;
    };

    class RigidbodyComponent final : public Component
    {
    public:
        RigidbodyComponent(Scene& scene, GameObject& owner) noexcept;
        ~RigidbodyComponent() override = default;

        [[nodiscard]] const RigidbodyDesc& GetDesc() const noexcept;
        void SetDesc(const RigidbodyDesc& desc) noexcept;

        [[nodiscard]] RigidbodyBackend& GetBackend() noexcept;
        [[nodiscard]] const RigidbodyBackend& GetBackend() const noexcept;

        [[nodiscard]] Float32 GetMass() const noexcept;
        void SetMass(Float32 mass) noexcept;

        [[nodiscard]] Float32 GetLinearDamping() const noexcept;
        void SetLinearDamping(Float32 linearDamping) noexcept;

        [[nodiscard]] Float32 GetAngularDamping() const noexcept;
        void SetAngularDamping(Float32 angularDamping) noexcept;

        [[nodiscard]] bool UsesGravity() const noexcept;
        void SetUseGravity(bool useGravity) noexcept;

        [[nodiscard]] bool IsKinematic() const noexcept;
        void SetKinematic(bool kinematic) noexcept;

        [[nodiscard]] bool DetectsCollisions() const noexcept;
        void SetDetectCollisions(bool detectCollisions) noexcept;

        [[nodiscard]] RigidbodyInterpolationMode GetInterpolationMode() const noexcept;
        void SetInterpolationMode(RigidbodyInterpolationMode interpolationMode) noexcept;

        [[nodiscard]] RigidbodyCollisionDetectionMode GetCollisionDetectionMode() const noexcept;
        void SetCollisionDetectionMode(RigidbodyCollisionDetectionMode collisionDetectionMode) noexcept;

        [[nodiscard]] RigidbodyConstraintFlags GetConstraints() const noexcept;
        void SetConstraints(RigidbodyConstraintFlags constraints) noexcept;
        [[nodiscard]] bool HasConstraint(RigidbodyConstraintFlags constraint) const noexcept;

        [[nodiscard]] PhysicsBodyHandle GetPhysicsBodyHandle() const noexcept;
        void SetPhysicsBodyHandle(PhysicsBodyHandle bodyHandle) noexcept;
        void ClearPhysicsBodyHandle() noexcept;

    private:
        void MarkBackendDirty() noexcept;

        RigidbodyDesc desc_;
        RigidbodyBackend backend_;
    };
} // namespace ve
