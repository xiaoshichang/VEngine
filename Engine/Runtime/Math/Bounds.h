#pragma once

#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector3.h"

#include <algorithm>
#include <cmath>

namespace ve
{
    class Aabb
    {
    public:
        constexpr Aabb(const Vector3& minimum, const Vector3& maximum) noexcept
            : minimum_(minimum)
            , maximum_(maximum)
        {
        }

        [[nodiscard]] static constexpr Aabb FromCenterExtents(const Vector3& center, const Vector3& extents) noexcept
        {
            return Aabb(center - extents, center + extents);
        }

        [[nodiscard]] bool IsFiniteAndValid() const noexcept
        {
            return std::isfinite(minimum_.GetX()) && std::isfinite(minimum_.GetY()) && std::isfinite(minimum_.GetZ()) && std::isfinite(maximum_.GetX()) &&
                   std::isfinite(maximum_.GetY()) && std::isfinite(maximum_.GetZ()) && minimum_.GetX() <= maximum_.GetX() &&
                   minimum_.GetY() <= maximum_.GetY() && minimum_.GetZ() <= maximum_.GetZ();
        }

        [[nodiscard]] constexpr const Vector3& GetMinimum() const noexcept
        {
            return minimum_;
        }

        [[nodiscard]] constexpr const Vector3& GetMaximum() const noexcept
        {
            return maximum_;
        }

        [[nodiscard]] constexpr Vector3 GetCenter() const noexcept
        {
            return (minimum_ + maximum_) * 0.5f;
        }

        [[nodiscard]] constexpr Vector3 GetExtents() const noexcept
        {
            return (maximum_ - minimum_) * 0.5f;
        }

        [[nodiscard]] Aabb Transformed(const Matrix44& transform) const noexcept
        {
            const Vector3 firstCorner = transform.TransformPoint(minimum_);
            Vector3 transformedMinimum = firstCorner;
            Vector3 transformedMaximum = firstCorner;

            for (UInt32 cornerIndex = 1; cornerIndex < 8; ++cornerIndex)
            {
                const Vector3 corner((cornerIndex & 1u) != 0u ? maximum_.GetX() : minimum_.GetX(),
                                     (cornerIndex & 2u) != 0u ? maximum_.GetY() : minimum_.GetY(),
                                     (cornerIndex & 4u) != 0u ? maximum_.GetZ() : minimum_.GetZ());
                const Vector3 transformedCorner = transform.TransformPoint(corner);
                transformedMinimum.SetX(std::min(transformedMinimum.GetX(), transformedCorner.GetX()));
                transformedMinimum.SetY(std::min(transformedMinimum.GetY(), transformedCorner.GetY()));
                transformedMinimum.SetZ(std::min(transformedMinimum.GetZ(), transformedCorner.GetZ()));
                transformedMaximum.SetX(std::max(transformedMaximum.GetX(), transformedCorner.GetX()));
                transformedMaximum.SetY(std::max(transformedMaximum.GetY(), transformedCorner.GetY()));
                transformedMaximum.SetZ(std::max(transformedMaximum.GetZ(), transformedCorner.GetZ()));
            }

            return Aabb(transformedMinimum, transformedMaximum);
        }

        [[nodiscard]] constexpr bool Intersects(const Aabb& other) const noexcept
        {
            return minimum_.GetX() <= other.maximum_.GetX() && maximum_.GetX() >= other.minimum_.GetX() && minimum_.GetY() <= other.maximum_.GetY() &&
                   maximum_.GetY() >= other.minimum_.GetY() && minimum_.GetZ() <= other.maximum_.GetZ() && maximum_.GetZ() >= other.minimum_.GetZ();
        }

    private:
        Vector3 minimum_;
        Vector3 maximum_;
    };
} // namespace ve
