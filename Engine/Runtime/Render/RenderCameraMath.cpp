#include "Engine/Runtime/Render/RenderCameraMath.h"

#include "Engine/Runtime/Render/RenderScene.h"

#include <algorithm>
#include <cmath>

namespace ve
{
    namespace
    {
        [[nodiscard]] Float32 ResolveAspectRatio(const RTCamera& camera, rhi::RhiExtent2D targetExtent) noexcept
        {
            if (!camera.IsAspectRatioAutomatic())
            {
                return std::max(camera.GetAspectRatio(), 0.001f);
            }

            const Float32 width = static_cast<Float32>(std::max(targetExtent.width, 1u));
            const Float32 height = static_cast<Float32>(std::max(targetExtent.height, 1u));
            return width / height;
        }
    } // namespace

    Matrix44 BuildPerspectiveProjection(Float32 verticalFov, Float32 aspect, Float32 nearClip, Float32 farClip) noexcept
    {
        const Float32 resolvedNearClip = std::max(nearClip, 0.001f);
        const Float32 resolvedFarClip = std::max(farClip, resolvedNearClip + 0.001f);
        const Float32 resolvedFieldOfView = std::max(verticalFov, 0.001f);
        const Float32 resolvedAspect = std::max(aspect, 0.001f);
        const Float32 yScale = 1.0f / std::tan(resolvedFieldOfView * 0.5f);
        const Float32 xScale = yScale / resolvedAspect;

        Matrix44 projection = Matrix44::Zero();
        projection.Set(0, 0, xScale);
        projection.Set(1, 1, yScale);
        projection.Set(2, 2, resolvedFarClip / (resolvedFarClip - resolvedNearClip));
        projection.Set(2, 3, -(resolvedNearClip * resolvedFarClip) / (resolvedFarClip - resolvedNearClip));
        projection.Set(3, 2, 1.0f);
        return projection;
    }

    Matrix44 BuildOrthographicProjection(Float32 height, Float32 aspect, Float32 nearClip, Float32 farClip) noexcept
    {
        const Float32 resolvedHeight = std::max(height, 0.001f);
        const Float32 resolvedAspect = std::max(aspect, 0.001f);
        const Float32 resolvedFarClip = std::max(farClip, nearClip + 0.001f);
        const Float32 width = resolvedHeight * resolvedAspect;

        Matrix44 projection = Matrix44::Identity();
        projection.Set(0, 0, 2.0f / width);
        projection.Set(1, 1, 2.0f / resolvedHeight);
        projection.Set(2, 2, 1.0f / (resolvedFarClip - nearClip));
        projection.Set(2, 3, -nearClip / (resolvedFarClip - nearClip));
        return projection;
    }

    Matrix44 BuildRigidView(const Matrix44& localToWorld) noexcept
    {
        Matrix44 inverse = Matrix44::Identity();
        for (SizeT row = 0; row < 3; ++row)
        {
            for (SizeT column = 0; column < 3; ++column)
            {
                inverse.Set(row, column, localToWorld.Get(column, row));
            }
        }

        const Vector3 translation(localToWorld.Get(0, 3), localToWorld.Get(1, 3), localToWorld.Get(2, 3));
        for (SizeT row = 0; row < 3; ++row)
        {
            const Float32 value =
                -((inverse.Get(row, 0) * translation.GetX()) + (inverse.Get(row, 1) * translation.GetY()) + (inverse.Get(row, 2) * translation.GetZ()));
            inverse.Set(row, 3, value);
        }

        return inverse;
    }

    Matrix44 BuildCameraViewProjection(const RTCamera& camera, rhi::RhiExtent2D targetExtent) noexcept
    {
        const Float32 aspectRatio = ResolveAspectRatio(camera, targetExtent);
        const Matrix44 projection =
            camera.GetProjectionMode() == RTCameraProjectionMode::Orthographic
                ? BuildOrthographicProjection(camera.GetOrthographicSize(), aspectRatio, camera.GetNearClipPlane(), camera.GetFarClipPlane())
                : BuildPerspectiveProjection(camera.GetVerticalFieldOfViewRadians(), aspectRatio, camera.GetNearClipPlane(), camera.GetFarClipPlane());
        return projection * BuildRigidView(camera.GetLocalToWorld());
    }
} // namespace ve
