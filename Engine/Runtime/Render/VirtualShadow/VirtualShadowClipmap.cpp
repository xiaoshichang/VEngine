#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h"

#include <cmath>

namespace ve
{
    VirtualShadowClipmapSet BuildVirtualShadowClipmaps(const Matrix44& cameraLocalToWorld, Vector3 lightDirection, Float32 shadowDistance) noexcept
    {
        VirtualShadowClipmapSet result;
        if (!std::isfinite(shadowDistance) || shadowDistance <= Math::DefaultEpsilon || !std::isfinite(lightDirection.GetX()) ||
            !std::isfinite(lightDirection.GetY()) || !std::isfinite(lightDirection.GetZ()))
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
        if (!std::isfinite(result.depthStep) || result.depthStep <= 0.0f)
        {
            return {};
        }

        const Vector3 cameraPosition(cameraLocalToWorld.Get(0, 3), cameraLocalToWorld.Get(1, 3), cameraLocalToWorld.Get(2, 3));
        const Vector3 lightPosition = result.lightBasis.TransformPoint(cameraPosition);
        Int32 depthEpoch = 0;
        if (!TryQuantizeVirtualShadowCoordinate(lightPosition.GetZ(), result.depthStep, depthEpoch) || !IsVirtualShadowDepthEpochRepresentable(depthEpoch))
        {
            return {};
        }

        for (UInt32 levelIndex = 0; levelIndex < VirtualShadowClipmapLevelCount; ++levelIndex)
        {
            VirtualShadowClipmapLevel& level = result.levels[levelIndex];
            level.level = levelIndex;
            level.worldRadius = shadowDistance / static_cast<Float32>(1u << (VirtualShadowClipmapLevelCount - 1u - levelIndex));
            level.pageWorldSize = (2.0f * level.worldRadius) / static_cast<Float32>(VirtualShadowPagesPerAxis);
            Int32 minimumPage = 0;
            Int32 maximumPage = 0;
            if (!std::isfinite(level.worldRadius) || !std::isfinite(level.pageWorldSize) || level.pageWorldSize <= 0.0f ||
                !TryQuantizeVirtualShadowCoordinate(lightPosition.GetX(), level.pageWorldSize, level.originPageX) ||
                !TryQuantizeVirtualShadowCoordinate(lightPosition.GetY(), level.pageWorldSize, level.originPageY) ||
                !TryBuildVirtualShadowWorkingRegion(level.originPageX, minimumPage, maximumPage) ||
                !TryBuildVirtualShadowWorkingRegion(level.originPageY, minimumPage, maximumPage))
            {
                return {};
            }
            level.depthEpoch = depthEpoch;
        }

        result.valid = true;
        return result;
    }

} // namespace ve
