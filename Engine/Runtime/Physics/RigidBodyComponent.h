#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
    enum class RigidBodyType
    {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class PhysicsInterpolationMode
    {
        None,
        Interpolate,
        Extrapolate,
    };

    class RigidBodyComponent final : public Component
    {
    public:
        [[nodiscard]] RigidBodyType GetBodyType() const noexcept;
        void SetBodyType(RigidBodyType type);

        [[nodiscard]] bool IsKinematic() const noexcept;
        void SetKinematic(bool kinematic);

        [[nodiscard]] Float32 GetMass() const noexcept;
        void SetMass(Float32 mass);

        [[nodiscard]] bool UsesGravity() const noexcept;
        void SetUseGravity(bool useGravity);

        [[nodiscard]] Float32 GetGravityScale() const noexcept;
        void SetGravityScale(Float32 gravityScale);

        [[nodiscard]] const Vector3& GetLinearVelocity() const noexcept;
        void SetLinearVelocity(const Vector3& velocity);

        [[nodiscard]] const Vector3& GetAngularVelocity() const noexcept;
        void SetAngularVelocity(const Vector3& velocity);

        [[nodiscard]] Float32 GetLinearDamping() const noexcept;
        void SetLinearDamping(Float32 damping);

        [[nodiscard]] Float32 GetAngularDamping() const noexcept;
        void SetAngularDamping(Float32 damping);

        [[nodiscard]] PhysicsInterpolationMode GetInterpolationMode() const noexcept;
        void SetInterpolationMode(PhysicsInterpolationMode mode);

        [[nodiscard]] const Vector3& GetAccumulatedForce() const noexcept;
        [[nodiscard]] const Vector3& GetAccumulatedTorque() const noexcept;
        void AddForce(const Vector3& force);
        void AddTorque(const Vector3& torque);
        void AddImpulse(const Vector3& impulse);
        void AddAngularImpulse(const Vector3& angularImpulse);
        void ClearAccumulators() noexcept;

    private:
        RigidBodyType bodyType_ = RigidBodyType::Dynamic;
        Float32 mass_ = 1.0f;
        bool useGravity_ = true;
        Float32 gravityScale_ = 1.0f;
        Vector3 linearVelocity_ = Vector3::Zero();
        Vector3 angularVelocity_ = Vector3::Zero();
        Float32 linearDamping_ = 0.0f;
        Float32 angularDamping_ = 0.0f;
        PhysicsInterpolationMode interpolationMode_ = PhysicsInterpolationMode::Interpolate;
        Vector3 accumulatedForce_ = Vector3::Zero();
        Vector3 accumulatedTorque_ = Vector3::Zero();
    };
} // namespace ve
