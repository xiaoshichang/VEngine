#include "Engine/Runtime/Physics/RigidBodyComponent.h"

#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Scene/Scene.h"

#include <algorithm>
#include <cmath>

namespace
{
    [[nodiscard]] ve::Float32 SanitizeNonNegative(ve::Float32 value) noexcept
    {
        return std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
    }
} // namespace

namespace ve
{
    RigidBodyType RigidBodyComponent::GetBodyType() const noexcept
    {
        return bodyType_;
    }

    void RigidBodyComponent::SetBodyType(RigidBodyType type)
    {
        GetScene().ValidateMutationAccess();
        bodyType_ = type;
        if (bodyType_ == RigidBodyType::Dynamic && interpolationMode_ == PhysicsInterpolationMode::None)
        {
            interpolationMode_ = PhysicsInterpolationMode::Interpolate;
        }
    }

    bool RigidBodyComponent::IsKinematic() const noexcept
    {
        return bodyType_ == RigidBodyType::Kinematic;
    }

    void RigidBodyComponent::SetKinematic(bool kinematic)
    {
        SetBodyType(kinematic ? RigidBodyType::Kinematic : RigidBodyType::Dynamic);
    }

    Float32 RigidBodyComponent::GetMass() const noexcept
    {
        return mass_;
    }

    void RigidBodyComponent::SetMass(Float32 mass)
    {
        GetScene().ValidateMutationAccess();
        mass_ = std::isfinite(mass) && mass > Math::DefaultEpsilon ? mass : 1.0f;
    }

    bool RigidBodyComponent::UsesGravity() const noexcept
    {
        return useGravity_;
    }

    void RigidBodyComponent::SetUseGravity(bool useGravity)
    {
        GetScene().ValidateMutationAccess();
        useGravity_ = useGravity;
    }

    Float32 RigidBodyComponent::GetGravityScale() const noexcept
    {
        return gravityScale_;
    }

    void RigidBodyComponent::SetGravityScale(Float32 gravityScale)
    {
        GetScene().ValidateMutationAccess();
        gravityScale_ = std::isfinite(gravityScale) ? gravityScale : 1.0f;
    }

    const Vector3& RigidBodyComponent::GetLinearVelocity() const noexcept
    {
        return linearVelocity_;
    }

    void RigidBodyComponent::SetLinearVelocity(const Vector3& velocity)
    {
        GetScene().ValidateMutationAccess();
        linearVelocity_ = velocity;
    }

    const Vector3& RigidBodyComponent::GetAngularVelocity() const noexcept
    {
        return angularVelocity_;
    }

    void RigidBodyComponent::SetAngularVelocity(const Vector3& velocity)
    {
        GetScene().ValidateMutationAccess();
        angularVelocity_ = velocity;
    }

    Float32 RigidBodyComponent::GetLinearDamping() const noexcept
    {
        return linearDamping_;
    }

    void RigidBodyComponent::SetLinearDamping(Float32 damping)
    {
        GetScene().ValidateMutationAccess();
        linearDamping_ = SanitizeNonNegative(damping);
    }

    Float32 RigidBodyComponent::GetAngularDamping() const noexcept
    {
        return angularDamping_;
    }

    void RigidBodyComponent::SetAngularDamping(Float32 damping)
    {
        GetScene().ValidateMutationAccess();
        angularDamping_ = SanitizeNonNegative(damping);
    }

    PhysicsInterpolationMode RigidBodyComponent::GetInterpolationMode() const noexcept
    {
        return interpolationMode_;
    }

    void RigidBodyComponent::SetInterpolationMode(PhysicsInterpolationMode mode)
    {
        GetScene().ValidateMutationAccess();
        interpolationMode_ = mode;
    }

    const Vector3& RigidBodyComponent::GetAccumulatedForce() const noexcept
    {
        return accumulatedForce_;
    }

    const Vector3& RigidBodyComponent::GetAccumulatedTorque() const noexcept
    {
        return accumulatedTorque_;
    }

    void RigidBodyComponent::AddForce(const Vector3& force)
    {
        GetScene().ValidateMutationAccess();
        accumulatedForce_ += force;
    }

    void RigidBodyComponent::AddTorque(const Vector3& torque)
    {
        GetScene().ValidateMutationAccess();
        accumulatedTorque_ += torque;
    }

    void RigidBodyComponent::AddImpulse(const Vector3& impulse)
    {
        GetScene().ValidateMutationAccess();
        if (bodyType_ != RigidBodyType::Dynamic)
        {
            return;
        }

        linearVelocity_ += impulse / mass_;
    }

    void RigidBodyComponent::AddAngularImpulse(const Vector3& angularImpulse)
    {
        GetScene().ValidateMutationAccess();
        if (bodyType_ == RigidBodyType::Dynamic)
        {
            angularVelocity_ += angularImpulse;
        }
    }

    void RigidBodyComponent::ClearAccumulators() noexcept
    {
        accumulatedForce_ = Vector3::Zero();
        accumulatedTorque_ = Vector3::Zero();
    }
} // namespace ve
