#include "Engine/Runtime/Physics/PhysicsGeometry.h"

#include "Engine/Runtime/Math/Math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
    [[nodiscard]] ve::Float32 Abs(ve::Float32 value) noexcept
    {
        return std::abs(value);
    }

    [[nodiscard]] ve::Float32 ComponentAt(const ve::Vector3& vector, ve::SizeT index) noexcept
    {
        switch (index)
        {
        case 0:
            return vector.GetX();
        case 1:
            return vector.GetY();
        default:
            return vector.GetZ();
        }
    }

    [[nodiscard]] ve::Vector3 NormalizeOrFallback(const ve::Vector3& axis, const ve::Vector3& fallback) noexcept
    {
        const ve::Vector3 normalized = axis.Normalized();
        return normalized == ve::Vector3::Zero() ? fallback : normalized;
    }

    [[nodiscard]] std::array<ve::Vector3, 3> BuildBoxAxes(const ve::OrientedBox& box) noexcept
    {
        return {NormalizeOrFallback(box.axes[0], ve::Vector3::UnitX()),
                NormalizeOrFallback(box.axes[1], ve::Vector3::UnitY()),
                NormalizeOrFallback(box.axes[2], ve::Vector3::UnitZ())};
    }

    [[nodiscard]] ve::Vector3 ClosestPointOnBox(const ve::Vector3& point,
                                                const ve::OrientedBox& box,
                                                const std::array<ve::Vector3, 3>& axes) noexcept
    {
        const ve::Vector3 offset = point - box.center;
        ve::Vector3 closest = box.center;

        for (ve::SizeT index = 0; index < axes.size(); ++index)
        {
            const ve::Float32 halfExtent = Abs(ComponentAt(box.halfExtents, index));
            const ve::Float32 distance = ve::Vector3::Dot(offset, axes[index]);
            closest += axes[index] * ve::Clamp(distance, -halfExtent, halfExtent);
        }

        return closest;
    }

    [[nodiscard]] ve::Float32 ProjectRadius(const ve::OrientedBox& box,
                                            const std::array<ve::Vector3, 3>& axes,
                                            const ve::Vector3& axis) noexcept
    {
        ve::Float32 radius = 0.0f;
        for (ve::SizeT index = 0; index < axes.size(); ++index)
        {
            radius += Abs(ComponentAt(box.halfExtents, index)) * Abs(ve::Vector3::Dot(axes[index], axis));
        }

        return radius;
    }

    [[nodiscard]] bool HasSeparatingAxis(const ve::OrientedBox& left,
                                         const std::array<ve::Vector3, 3>& leftAxes,
                                         const ve::OrientedBox& right,
                                         const std::array<ve::Vector3, 3>& rightAxes,
                                         const ve::Vector3& axis) noexcept
    {
        const ve::Vector3 normalizedAxis = axis.Normalized();
        if (normalizedAxis == ve::Vector3::Zero())
        {
            return false;
        }

        const ve::Float32 centerDistance = Abs(ve::Vector3::Dot(right.center - left.center, normalizedAxis));
        const ve::Float32 radius =
            ProjectRadius(left, leftAxes, normalizedAxis) + ProjectRadius(right, rightAxes, normalizedAxis);
        return centerDistance > radius + ve::Math::DefaultEpsilon;
    }
} // namespace

namespace ve
{
    Ray::Ray(const Vector3& rayOrigin, const Vector3& rayDirection) noexcept
        : origin(rayOrigin)
        , direction(rayDirection.Normalized())
    {
        if (direction == Vector3::Zero())
        {
            direction = Vector3::UnitZ();
        }
    }

    Vector3 Ray::GetPoint(Float32 distance) const noexcept
    {
        return origin + (direction * distance);
    }

    std::optional<ShapeRaycastHit> RaycastSphere(const Ray& ray, const Sphere& sphere) noexcept
    {
        const Vector3 direction = ray.direction.Normalized();
        if (direction == Vector3::Zero())
        {
            return std::nullopt;
        }

        const Vector3 toSphere = ray.origin - sphere.center;
        const Float32 a = Vector3::Dot(direction, direction);
        const Float32 b = 2.0f * Vector3::Dot(toSphere, direction);
        const Float32 c = Vector3::Dot(toSphere, toSphere) - (sphere.radius * sphere.radius);
        const Float32 discriminant = (b * b) - (4.0f * a * c);
        if (discriminant < -Math::DefaultEpsilon)
        {
            return std::nullopt;
        }

        const Float32 sqrtDiscriminant = Sqrt(discriminant < 0.0f ? 0.0f : discriminant);
        const Float32 inverseDenominator = 1.0f / (2.0f * a);
        Float32 distance = (-b - sqrtDiscriminant) * inverseDenominator;
        if (distance < 0.0f)
        {
            distance = (-b + sqrtDiscriminant) * inverseDenominator;
        }

        if (distance < 0.0f)
        {
            return std::nullopt;
        }

        ShapeRaycastHit hit;
        hit.distance = distance;
        hit.position = ray.origin + (direction * distance);
        hit.normal = (hit.position - sphere.center).Normalized();
        return hit;
    }

    std::optional<ShapeRaycastHit> RaycastOrientedBox(const Ray& ray, const OrientedBox& box) noexcept
    {
        const Vector3 direction = ray.direction.Normalized();
        if (direction == Vector3::Zero())
        {
            return std::nullopt;
        }

        const std::array<Vector3, 3> axes = BuildBoxAxes(box);
        const Vector3 toBox = box.center - ray.origin;
        Float32 enterDistance = -std::numeric_limits<Float32>::max();
        Float32 exitDistance = std::numeric_limits<Float32>::max();
        Vector3 enterNormal = Vector3::Zero();
        Vector3 exitNormal = Vector3::Zero();

        for (SizeT index = 0; index < axes.size(); ++index)
        {
            const Float32 halfExtent = Abs(ComponentAt(box.halfExtents, index));
            const Float32 originProjection = Vector3::Dot(axes[index], toBox);
            const Float32 directionProjection = Vector3::Dot(axes[index], direction);

            if (Abs(directionProjection) <= Math::DefaultEpsilon)
            {
                if (Abs(originProjection) > halfExtent + Math::DefaultEpsilon)
                {
                    return std::nullopt;
                }

                continue;
            }

            Float32 nearDistance = (originProjection - halfExtent) / directionProjection;
            Float32 farDistance = (originProjection + halfExtent) / directionProjection;
            Vector3 nearNormal = -axes[index];
            Vector3 farNormal = axes[index];
            if (nearDistance > farDistance)
            {
                std::swap(nearDistance, farDistance);
                std::swap(nearNormal, farNormal);
            }

            if (nearDistance > enterDistance)
            {
                enterDistance = nearDistance;
                enterNormal = nearNormal;
            }

            if (farDistance < exitDistance)
            {
                exitDistance = farDistance;
                exitNormal = farNormal;
            }

            if (enterDistance > exitDistance)
            {
                return std::nullopt;
            }
        }

        if (exitDistance < 0.0f)
        {
            return std::nullopt;
        }

        ShapeRaycastHit hit;
        hit.distance = enterDistance >= 0.0f ? enterDistance : exitDistance;
        hit.position = ray.origin + (direction * hit.distance);
        hit.normal = (enterDistance >= 0.0f ? enterNormal : exitNormal).Normalized();
        if (hit.normal == Vector3::Zero())
        {
            hit.normal = -direction;
        }

        return hit;
    }

    bool Overlaps(const Sphere& left, const Sphere& right) noexcept
    {
        const Float32 radiusSum = Abs(left.radius) + Abs(right.radius);
        return (right.center - left.center).LengthSquared() <= radiusSum * radiusSum;
    }

    bool Overlaps(const Sphere& sphere, const OrientedBox& box) noexcept
    {
        const std::array<Vector3, 3> axes = BuildBoxAxes(box);
        const Vector3 closest = ClosestPointOnBox(sphere.center, box, axes);
        const Float32 radius = Abs(sphere.radius);
        return (sphere.center - closest).LengthSquared() <= radius * radius;
    }

    bool Overlaps(const OrientedBox& left, const OrientedBox& right) noexcept
    {
        const std::array<Vector3, 3> leftAxes = BuildBoxAxes(left);
        const std::array<Vector3, 3> rightAxes = BuildBoxAxes(right);

        for (const Vector3& axis : leftAxes)
        {
            if (HasSeparatingAxis(left, leftAxes, right, rightAxes, axis))
            {
                return false;
            }
        }

        for (const Vector3& axis : rightAxes)
        {
            if (HasSeparatingAxis(left, leftAxes, right, rightAxes, axis))
            {
                return false;
            }
        }

        for (const Vector3& leftAxis : leftAxes)
        {
            for (const Vector3& rightAxis : rightAxes)
            {
                if (HasSeparatingAxis(left, leftAxes, right, rightAxes, Vector3::Cross(leftAxis, rightAxis)))
                {
                    return false;
                }
            }
        }

        return true;
    }

    Aabb BuildAabb(const Sphere& sphere) noexcept
    {
        const Float32 radius = Abs(sphere.radius);
        const Vector3 extents(radius, radius, radius);
        return Aabb{sphere.center - extents, sphere.center + extents};
    }

    Aabb BuildAabb(const OrientedBox& box) noexcept
    {
        const std::array<Vector3, 3> axes = BuildBoxAxes(box);
        Float32 extentX = 0.0f;
        Float32 extentY = 0.0f;
        Float32 extentZ = 0.0f;

        for (SizeT index = 0; index < axes.size(); ++index)
        {
            const Float32 halfExtent = Abs(ComponentAt(box.halfExtents, index));
            extentX += Abs(axes[index].GetX()) * halfExtent;
            extentY += Abs(axes[index].GetY()) * halfExtent;
            extentZ += Abs(axes[index].GetZ()) * halfExtent;
        }

        const Vector3 extents(extentX, extentY, extentZ);
        return Aabb{box.center - extents, box.center + extents};
    }
} // namespace ve
