#include "Engine/Runtime/Math/Frustum.h"

#include "Engine/Runtime/Math/Bounds.h"
#include "Engine/Runtime/Math/Matrix44.h"

#include <utility>

namespace ve
{
    Frustum::Frustum(std::array<Plane, 6> planes) noexcept
        : planes_(std::move(planes))
    {
    }

    Frustum Frustum::FromViewProjection(const Matrix44& viewProjection) noexcept
    {
        const auto extractCombinedPlane = [&viewProjection](SizeT row, Float32 rowScale)
        {
            return Plane(Vector3(viewProjection.Get(3, 0) + (viewProjection.Get(row, 0) * rowScale),
                                 viewProjection.Get(3, 1) + (viewProjection.Get(row, 1) * rowScale),
                                 viewProjection.Get(3, 2) + (viewProjection.Get(row, 2) * rowScale)),
                         viewProjection.Get(3, 3) + (viewProjection.Get(row, 3) * rowScale))
                .Normalized();
        };
        const Plane nearPlane =
            Plane(Vector3(viewProjection.Get(2, 0), viewProjection.Get(2, 1), viewProjection.Get(2, 2)), viewProjection.Get(2, 3)).Normalized();

        return Frustum(std::array<Plane, 6>{
            extractCombinedPlane(0, 1.0f),
            extractCombinedPlane(0, -1.0f),
            extractCombinedPlane(1, 1.0f),
            extractCombinedPlane(1, -1.0f),
            nearPlane,
            extractCombinedPlane(2, -1.0f),
        });
    }

    bool Frustum::Intersects(const Aabb& bounds) const noexcept
    {
        for (const Plane& plane : planes_)
        {
            const Vector3& normal = plane.GetNormal();
            const Vector3 positiveVertex(normal.GetX() >= 0.0f ? bounds.GetMaximum().GetX() : bounds.GetMinimum().GetX(),
                                         normal.GetY() >= 0.0f ? bounds.GetMaximum().GetY() : bounds.GetMinimum().GetY(),
                                         normal.GetZ() >= 0.0f ? bounds.GetMaximum().GetZ() : bounds.GetMinimum().GetZ());
            if (plane.SignedDistance(positiveVertex) < 0.0f)
            {
                return false;
            }
        }

        return true;
    }
} // namespace ve
