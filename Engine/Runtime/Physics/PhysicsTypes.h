#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"

namespace ve
{
    enum class PhysicsBackendType
    {
        Auto,
        Jolt,
    };

    enum class PhysicsBodyMotionType
    {
        Static,
        Kinematic,
        Dynamic,
    };

    enum class PhysicsShapeType
    {
        Sphere,
        Box,
        Capsule,
    };

    struct PhysicsShapeDesc
    {
        [[nodiscard]] static constexpr PhysicsShapeDesc CreateSphere(Float32 radius) noexcept
        {
            PhysicsShapeDesc desc;
            desc.type = PhysicsShapeType::Sphere;
            desc.radius = radius;
            return desc;
        }

        [[nodiscard]] static constexpr PhysicsShapeDesc CreateBox(Vector3 halfExtent) noexcept
        {
            PhysicsShapeDesc desc;
            desc.type = PhysicsShapeType::Box;
            desc.halfExtent = halfExtent;
            return desc;
        }

        [[nodiscard]] static constexpr PhysicsShapeDesc CreateCapsule(Float32 radius, Float32 halfHeight) noexcept
        {
            PhysicsShapeDesc desc;
            desc.type = PhysicsShapeType::Capsule;
            desc.radius = radius;
            desc.halfHeight = halfHeight;
            return desc;
        }

        PhysicsShapeType type = PhysicsShapeType::Sphere;
        Float32 radius = 0.5f;
        Float32 halfHeight = 0.5f;
        Vector3 halfExtent = Vector3(0.5f, 0.5f, 0.5f);
    };

    struct PhysicsBodyTransform
    {
        Vector3 position = Vector3::Zero();
        Quaternion rotation = Quaternion::Identity();
    };

    class PhysicsBodyHandle
    {
    public:
        static constexpr UInt32 InvalidRawValue = 0xffffffffu;

        PhysicsBodyHandle() = default;

        [[nodiscard]] static constexpr PhysicsBodyHandle FromRawValue(UInt32 rawValue) noexcept
        {
            return PhysicsBodyHandle(rawValue);
        }

        [[nodiscard]] constexpr bool IsValid() const noexcept
        {
            return rawValue_ != InvalidRawValue;
        }

        [[nodiscard]] constexpr UInt32 GetRawValue() const noexcept
        {
            return rawValue_;
        }

        constexpr void Reset() noexcept
        {
            rawValue_ = InvalidRawValue;
        }

        [[nodiscard]] friend constexpr bool operator==(PhysicsBodyHandle left, PhysicsBodyHandle right) noexcept
        {
            return left.rawValue_ == right.rawValue_;
        }

        [[nodiscard]] friend constexpr bool operator!=(PhysicsBodyHandle left, PhysicsBodyHandle right) noexcept
        {
            return !(left == right);
        }

    private:
        explicit constexpr PhysicsBodyHandle(UInt32 rawValue) noexcept
            : rawValue_(rawValue)
        {
        }

        UInt32 rawValue_ = InvalidRawValue;
    };

    struct PhysicsBodyDesc
    {
        PhysicsShapeDesc shape;
        PhysicsBodyMotionType motionType = PhysicsBodyMotionType::Dynamic;
        PhysicsBodyTransform transform;
        Vector3 linearVelocity = Vector3::Zero();
        Vector3 angularVelocity = Vector3::Zero();
        Float32 mass = 1.0f;
        Float32 linearDamping = 0.0f;
        Float32 angularDamping = 0.05f;
        Float32 gravityFactor = 1.0f;
        Float32 friction = 0.2f;
        Float32 restitution = 0.0f;
        UInt64 userData = 0;
        bool startActive = true;
        bool trigger = false;
    };

    struct PhysicsSystemInitParam
    {
        PhysicsBackendType backendType = PhysicsBackendType::Auto;
        Vector3 gravity = Vector3(0.0f, -9.81f, 0.0f);
        UInt32 maxBodies = 1024;
        UInt32 bodyMutexCount = 0;
        UInt32 maxBodyPairs = 1024;
        UInt32 maxContactConstraints = 1024;
        UInt32 tempAllocatorSizeBytes = 10 * 1024 * 1024;
    };

    struct PhysicsStepDesc
    {
        Float32 deltaSeconds = 1.0f / 60.0f;
        UInt32 collisionSteps = 1;
    };
} // namespace ve
