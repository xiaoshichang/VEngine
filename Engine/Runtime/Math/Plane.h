#pragma once

#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Vector3.h"

namespace ve
{
    class Plane
    {
    public:
        constexpr Plane(const Vector3& normal, Float32 distance) noexcept
            : normal_(normal)
            , distance_(distance)
        {
        }

        [[nodiscard]] Plane Normalized() const noexcept
        {
            const Float32 length = normal_.Length();
            if (length <= Math::DefaultEpsilon)
            {
                return *this;
            }

            return Plane(normal_ / length, distance_ / length);
        }

        [[nodiscard]] constexpr Float32 SignedDistance(const Vector3& point) const noexcept
        {
            return Vector3::Dot(normal_, point) + distance_;
        }

        [[nodiscard]] constexpr const Vector3& GetNormal() const noexcept
        {
            return normal_;
        }

    private:
        Vector3 normal_;
        Float32 distance_ = 0.0f;
    };
} // namespace ve
