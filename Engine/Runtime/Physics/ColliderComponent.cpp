#include "Engine/Runtime/Physics/ColliderComponent.h"

#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    struct BoxBasis
    {
        std::array<ve::Vector3, 3> axes = {ve::Vector3::UnitX(), ve::Vector3::UnitY(), ve::Vector3::UnitZ()};
        ve::Vector3 scale = ve::Vector3::One();
    };

    [[nodiscard]] ve::Float32 Abs(ve::Float32 value) noexcept
    {
        return std::abs(value);
    }

    [[nodiscard]] ve::Float32 Max3(ve::Float32 x, ve::Float32 y, ve::Float32 z) noexcept
    {
        return std::max(x, std::max(y, z));
    }

    [[nodiscard]] ve::Vector3 NormalizeOrFallback(const ve::Vector3& value,
                                                  const ve::Vector3& fallback) noexcept
    {
        const ve::Vector3 normalized = value.Normalized();
        return normalized == ve::Vector3::Zero() ? fallback : normalized;
    }

    [[nodiscard]] ve::Vector3 PerpendicularTo(const ve::Vector3& axis) noexcept
    {
        const ve::Vector3 reference = Abs(axis.GetX()) < 0.9f ? ve::Vector3::UnitX() : ve::Vector3::UnitY();
        return NormalizeOrFallback(ve::Vector3::Cross(reference, axis), ve::Vector3::UnitZ());
    }

    [[nodiscard]] BoxBasis BuildBoxBasis(const ve::Matrix44& world) noexcept
    {
        const ve::Vector3 rawX = world.TransformDirection(ve::Vector3::UnitX());
        const ve::Vector3 rawY = world.TransformDirection(ve::Vector3::UnitY());
        const ve::Vector3 rawZ = world.TransformDirection(ve::Vector3::UnitZ());
        const ve::Float32 scaleX = rawX.Length();
        const ve::Float32 scaleY = rawY.Length();
        const ve::Float32 scaleZ = rawZ.Length();

        const ve::Vector3 axisX =
            scaleX > ve::Math::DefaultEpsilon ? rawX / scaleX : ve::Vector3::UnitX();
        ve::Vector3 axisY = rawY - (axisX * ve::Vector3::Dot(rawY, axisX));
        if (axisY.Length() <= ve::Math::DefaultEpsilon)
        {
            axisY = rawZ - (axisX * ve::Vector3::Dot(rawZ, axisX));
        }
        if (axisY.Length() <= ve::Math::DefaultEpsilon)
        {
            axisY = PerpendicularTo(axisX);
        }
        axisY = NormalizeOrFallback(axisY, PerpendicularTo(axisX));

        ve::Vector3 axisZ = NormalizeOrFallback(ve::Vector3::Cross(axisX, axisY), ve::Vector3::UnitZ());
        if (ve::Vector3::Dot(axisZ, rawZ) < 0.0f)
        {
            axisY = -axisY;
            axisZ = -axisZ;
        }

        return BoxBasis{{axisX, axisY, axisZ}, ve::Vector3(scaleX, scaleY, scaleZ)};
    }
} // namespace

namespace ve
{
    ColliderShape ColliderComponent::GetShape() const noexcept
    {
        return shape_;
    }

    void ColliderComponent::SetShape(ColliderShape shape)
    {
        GetScene().ValidateMutationAccess();
        shape_ = shape;
    }

    const Vector3& ColliderComponent::GetCenter() const noexcept
    {
        return center_;
    }

    void ColliderComponent::SetCenter(const Vector3& center)
    {
        GetScene().ValidateMutationAccess();
        center_ = center;
    }

    const Vector3& ColliderComponent::GetBoxSize() const noexcept
    {
        return boxSize_;
    }

    void ColliderComponent::SetBoxSize(const Vector3& size)
    {
        GetScene().ValidateMutationAccess();
        boxSize_ = size;
    }

    Float32 ColliderComponent::GetSphereRadius() const noexcept
    {
        return sphereRadius_;
    }

    void ColliderComponent::SetSphereRadius(Float32 radius)
    {
        GetScene().ValidateMutationAccess();
        sphereRadius_ = radius;
    }

    UInt64 ColliderComponent::GetLayer() const noexcept
    {
        return layer_;
    }

    void ColliderComponent::SetLayer(UInt64 layer)
    {
        GetScene().ValidateMutationAccess();
        layer_ = layer;
    }

    UInt64 ColliderComponent::GetCollidesWith() const noexcept
    {
        return collidesWith_;
    }

    void ColliderComponent::SetCollidesWith(UInt64 mask)
    {
        GetScene().ValidateMutationAccess();
        collidesWith_ = mask;
    }

    bool ColliderComponent::IsTrigger() const noexcept
    {
        return isTrigger_;
    }

    void ColliderComponent::SetTrigger(bool trigger)
    {
        GetScene().ValidateMutationAccess();
        isTrigger_ = trigger;
    }

    bool ColliderComponent::IsColliderEnabled() const noexcept
    {
        return enabled_;
    }

    void ColliderComponent::SetColliderEnabled(bool enabled)
    {
        GetScene().ValidateMutationAccess();
        enabled_ = enabled;
    }

    Sphere ColliderComponent::BuildWorldSphere(const TransformComponent& transform) const noexcept
    {
        const Matrix44& world = transform.GetWorldMatrix();
        const Vector3 axisX = world.TransformDirection(Vector3::UnitX());
        const Vector3 axisY = world.TransformDirection(Vector3::UnitY());
        const Vector3 axisZ = world.TransformDirection(Vector3::UnitZ());
        const Float32 scale = Max3(axisX.Length(), axisY.Length(), axisZ.Length());
        return Sphere{world.TransformPoint(center_), std::max(0.0f, sphereRadius_) * scale};
    }

    OrientedBox ColliderComponent::BuildWorldBox(const TransformComponent& transform) const noexcept
    {
        const Matrix44& world = transform.GetWorldMatrix();
        const BoxBasis basis = BuildBoxBasis(world);
        return OrientedBox{
            world.TransformPoint(center_),
            Vector3(Abs(boxSize_.GetX()) * basis.scale.GetX() * 0.5f,
                    Abs(boxSize_.GetY()) * basis.scale.GetY() * 0.5f,
                    Abs(boxSize_.GetZ()) * basis.scale.GetZ() * 0.5f),
            basis.axes,
        };
    }
} // namespace ve
