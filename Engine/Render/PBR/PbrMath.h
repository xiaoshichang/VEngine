#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Math/Vector4.h"
#include "Engine/Render/PBR/PbrTypes.h"

#include <cmath>

namespace ve::pbr
{
    [[nodiscard]] Float32 Saturate(Float32 value) noexcept;
    [[nodiscard]] Vector3 Saturate(const Vector3& value) noexcept;
    [[nodiscard]] Vector3 SrgbToLinear(const Vector3& value) noexcept;
    [[nodiscard]] Vector3 LinearToSrgb(const Vector3& value) noexcept;
    [[nodiscard]] Vector3 ReinhardToneMap(const Vector3& hdrColor) noexcept;
    [[nodiscard]] Vector3 AcesToneMap(const Vector3& hdrColor, Float32 whitePoint = 1.0f) noexcept;
    [[nodiscard]] Vector3 ApplyToneMapping(const Vector3& hdrColor, Float32 exposure, ToneMappingMode mode, Float32 whitePoint = 1.0f) noexcept;

    [[nodiscard]] Float32 DistributionGgx(Float32 nDotH, Float32 roughness) noexcept;
    [[nodiscard]] Float32 GeometrySchlickGgx(Float32 nDotV, Float32 roughness) noexcept;
    [[nodiscard]] Float32 GeometrySmith(Float32 nDotV, Float32 nDotL, Float32 roughness) noexcept;
    [[nodiscard]] Vector3 FresnelSchlick(Float32 cosTheta, const Vector3& f0) noexcept;
    [[nodiscard]] Vector3 FresnelSchlickRoughness(Float32 cosTheta, const Vector3& f0, Float32 roughness) noexcept;

    /// Evaluates one Cook-Torrance direct-light contribution. Inputs are expected to be normalized.
    [[nodiscard]] Vector3 EvaluateDirectionalLight(const Vector3& normal,
                                                   const Vector3& viewDirection,
                                                   const Vector3& lightDirection,
                                                   const Vector3& baseColor,
                                                   Float32 metallic,
                                                   Float32 roughness,
                                                   const Vector3& lightColor,
                                                   Float32 lightIntensity) noexcept;
} // namespace ve::pbr
