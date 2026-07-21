#pragma once

#include "Engine/Runtime/Math/Bounds.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h"

#include <array>

namespace ve
{
    struct VirtualShadowLightBasis
    {
        Vector3 right = Vector3::UnitX();
        Vector3 up = Vector3::UnitY();
        Vector3 forward = Vector3::UnitZ();

        [[nodiscard]] Vector3 TransformPoint(const Vector3& worldPoint) const noexcept
        {
            return Vector3(Vector3::Dot(worldPoint, right), Vector3::Dot(worldPoint, up), Vector3::Dot(worldPoint, forward));
        }
    };

    struct VirtualShadowClipmapLevel
    {
        UInt32 level = 0;
        Float32 worldRadius = 0.0f;
        Float32 pageWorldSize = 0.0f;
        Int32 originPageX = 0;
        Int32 originPageY = 0;
        Int32 depthEpoch = 0;
    };

    struct VirtualShadowClipmapSet
    {
        std::array<VirtualShadowClipmapLevel, VirtualShadowClipmapLevelCount> levels{};
        VirtualShadowLightBasis lightBasis;
        Float32 shadowDistance = 0.0f;
        Float32 depthStep = 0.0f;
        bool valid = false;
    };

    [[nodiscard]] VirtualShadowClipmapSet
    BuildVirtualShadowClipmaps(const Matrix44& cameraLocalToWorld, Vector3 lightDirection, Float32 shadowDistance) noexcept;
    [[nodiscard]] Aabb
    GetVirtualShadowPageLightSpaceBounds(const VirtualShadowClipmapSet& clipmaps, VirtualShadowPageKey key, Float32 gutterWorldSize = 0.0f) noexcept;
} // namespace ve
