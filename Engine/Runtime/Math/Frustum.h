#pragma once

#include "Engine/Runtime/Math/Plane.h"

#include <array>

namespace ve
{
    class Aabb;
    class Matrix44;

    class Frustum
    {
    public:
        [[nodiscard]] static Frustum FromViewProjection(const Matrix44& viewProjection) noexcept;

        [[nodiscard]] bool Intersects(const Aabb& bounds) const noexcept;

    private:
        explicit Frustum(std::array<Plane, 6> planes) noexcept;

    private:
        std::array<Plane, 6> planes_;
    };
} // namespace ve
