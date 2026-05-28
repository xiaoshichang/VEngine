#include "Engine/Runtime/Physics/PhysicsGeometry.h"

#include "Engine/Runtime/Math/Math.h"

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
} // namespace ve
