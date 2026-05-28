#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"

#include <array>
#include <optional>

namespace ve
{
    struct Ray
    {
        Vector3 origin = Vector3::Zero();
        Vector3 direction = Vector3::UnitZ();

        Ray() = default;
        Ray(const Vector3& rayOrigin, const Vector3& rayDirection) noexcept;

        [[nodiscard]] Vector3 GetPoint(Float32 distance) const noexcept;
    };

    struct Sphere
    {
        Vector3 center = Vector3::Zero();
        Float32 radius = 0.5f;
    };

    struct OrientedBox
    {
        Vector3 center = Vector3::Zero();
        Vector3 halfExtents = Vector3(0.5f, 0.5f, 0.5f);
        std::array<Vector3, 3> axes = {Vector3::UnitX(), Vector3::UnitY(), Vector3::UnitZ()};
    };

    struct Aabb
    {
        Vector3 minimum = Vector3::Zero();
        Vector3 maximum = Vector3::Zero();
    };

    struct ShapeRaycastHit
    {
        Float32 distance = 0.0f;
        Vector3 position = Vector3::Zero();
        Vector3 normal = Vector3::UnitY();
    };

    [[nodiscard]] std::optional<ShapeRaycastHit> RaycastSphere(const Ray& ray, const Sphere& sphere) noexcept;
    [[nodiscard]] std::optional<ShapeRaycastHit> RaycastOrientedBox(const Ray& ray,
                                                                    const OrientedBox& box) noexcept;
    [[nodiscard]] bool Overlaps(const Sphere& left, const Sphere& right) noexcept;
    [[nodiscard]] bool Overlaps(const Sphere& sphere, const OrientedBox& box) noexcept;
    [[nodiscard]] bool Overlaps(const OrientedBox& left, const OrientedBox& right) noexcept;
    [[nodiscard]] Aabb BuildAabb(const Sphere& sphere) noexcept;
    [[nodiscard]] Aabb BuildAabb(const OrientedBox& box) noexcept;
} // namespace ve
