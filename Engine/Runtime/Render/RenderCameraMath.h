#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Matrix44.h"

namespace ve
{
    class RTCamera;

    [[nodiscard]] Matrix44 BuildPerspectiveProjection(Float32 verticalFov, Float32 aspect, Float32 nearClip, Float32 farClip) noexcept;
    [[nodiscard]] Matrix44 BuildOrthographicProjection(Float32 height, Float32 aspect, Float32 nearClip, Float32 farClip) noexcept;
    [[nodiscard]] Matrix44 BuildRigidView(const Matrix44& localToWorld) noexcept;
    [[nodiscard]] Matrix44 BuildCameraViewProjection(const RTCamera& camera, rhi::RhiExtent2D targetExtent) noexcept;
} // namespace ve
