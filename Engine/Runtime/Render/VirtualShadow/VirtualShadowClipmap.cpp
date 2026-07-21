#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h"

#include <cmath>
#include <limits>

namespace ve
{
    namespace
    {
        Int32 QuantizeCoordinate(Float32 coordinate, Float32 step) noexcept
        {
            const Float32 quantized = std::floor(coordinate / step);
            if (quantized < static_cast<Float32>(std::numeric_limits<Int32>::min()) || quantized > static_cast<Float32>(std::numeric_limits<Int32>::max()))
            {
                return 0;
            }
            return static_cast<Int32>(quantized);
        }
    } // namespace

    VirtualShadowClipmapSet BuildVirtualShadowClipmaps(const Matrix44& cameraLocalToWorld, Vector3 lightDirection, Float32 shadowDistance) noexcept
    {
        VirtualShadowClipmapSet result;
        if (!std::isfinite(shadowDistance) || shadowDistance <= 0.0f || !std::isfinite(lightDirection.GetX()) || !std::isfinite(lightDirection.GetY()) ||
            !std::isfinite(lightDirection.GetZ()))
        {
            return result;
        }

        lightDirection = lightDirection.Normalized();
        if (lightDirection.LengthSquared() == 0.0f)
        {
            return result;
        }

        const Vector3 helper = std::abs(Vector3::Dot(lightDirection, Vector3::UnitY())) > 0.99f ? Vector3::UnitX() : Vector3::UnitY();
        result.lightBasis.right = Vector3::Cross(helper, lightDirection).Normalized();
        result.lightBasis.up = Vector3::Cross(lightDirection, result.lightBasis.right).Normalized();
        result.lightBasis.forward = lightDirection;
        result.shadowDistance = shadowDistance;
        result.depthStep = shadowDistance / 64.0f;

        const Vector3 cameraPosition(cameraLocalToWorld.Get(0, 3), cameraLocalToWorld.Get(1, 3), cameraLocalToWorld.Get(2, 3));
        const Vector3 lightPosition = result.lightBasis.TransformPoint(cameraPosition);
        const Int32 depthEpoch = QuantizeCoordinate(lightPosition.GetZ(), result.depthStep);

        for (UInt32 levelIndex = 0; levelIndex < VirtualShadowClipmapLevelCount; ++levelIndex)
        {
            VirtualShadowClipmapLevel& level = result.levels[levelIndex];
            level.level = levelIndex;
            level.worldRadius = shadowDistance / static_cast<Float32>(1u << (VirtualShadowClipmapLevelCount - 1u - levelIndex));
            level.pageWorldSize = (2.0f * level.worldRadius) / static_cast<Float32>(VirtualShadowPagesPerAxis);
            level.originPageX = QuantizeCoordinate(lightPosition.GetX(), level.pageWorldSize);
            level.originPageY = QuantizeCoordinate(lightPosition.GetY(), level.pageWorldSize);
            level.depthEpoch = depthEpoch;
        }

        result.valid = true;
        return result;
    }

    Aabb GetVirtualShadowPageLightSpaceBounds(const VirtualShadowClipmapSet& clipmaps, VirtualShadowPageKey key, Float32 gutterWorldSize) noexcept
    {
        if (!clipmaps.valid || !key.IsValid() || key.GetClipmapLevel() >= VirtualShadowClipmapLevelCount)
        {
            return Aabb(Vector3::Zero(), Vector3(-1.0f, -1.0f, -1.0f));
        }

        const VirtualShadowClipmapLevel& level = clipmaps.levels[key.GetClipmapLevel()];
        const Float32 minimumX = static_cast<Float32>(key.GetPageX()) * level.pageWorldSize - gutterWorldSize;
        const Float32 minimumY = static_cast<Float32>(key.GetPageY()) * level.pageWorldSize - gutterWorldSize;
        const Float32 depthCenter = static_cast<Float32>(key.GetDepthEpoch()) * clipmaps.depthStep;
        return Aabb(Vector3(minimumX, minimumY, depthCenter - clipmaps.shadowDistance),
                    Vector3(minimumX + level.pageWorldSize + (2.0f * gutterWorldSize),
                            minimumY + level.pageWorldSize + (2.0f * gutterWorldSize),
                            depthCenter + clipmaps.shadowDistance));
    }
} // namespace ve
