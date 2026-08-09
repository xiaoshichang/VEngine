#include "Engine/Render/PBR/PbrMath.h"

#include "Engine/Render/PBR/PbrTypes.h"
#include "Engine/Runtime/Math/Math.h"

#include <algorithm>

namespace ve::pbr
{
    namespace
    {
        constexpr Float32 Pi = 3.14159265358979323846f;
        constexpr Float32 MinDenominator = 0.0001f;

        [[nodiscard]] Float32 Pow5(Float32 value) noexcept
        {
            const Float32 squared = value * value;
            return squared * squared * value;
        }

        [[nodiscard]] Float32 LinearizeChannel(Float32 value) noexcept
        {
            return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }

        [[nodiscard]] Float32 EncodeSrgbChannel(Float32 value) noexcept
        {
            value = std::max(value, 0.0f);
            return value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
        }

        [[nodiscard]] Vector3 ComponentMultiply(const Vector3& left, const Vector3& right) noexcept
        {
            return Vector3(left.GetX() * right.GetX(), left.GetY() * right.GetY(), left.GetZ() * right.GetZ());
        }
    } // namespace

    Float32 Saturate(Float32 value) noexcept
    {
        return std::clamp(value, 0.0f, 1.0f);
    }

    Vector3 Saturate(const Vector3& value) noexcept
    {
        return Vector3(Saturate(value.GetX()), Saturate(value.GetY()), Saturate(value.GetZ()));
    }

    Vector3 SrgbToLinear(const Vector3& value) noexcept
    {
        return Vector3(LinearizeChannel(value.GetX()), LinearizeChannel(value.GetY()), LinearizeChannel(value.GetZ()));
    }

    Vector3 LinearToSrgb(const Vector3& value) noexcept
    {
        return Vector3(EncodeSrgbChannel(value.GetX()), EncodeSrgbChannel(value.GetY()), EncodeSrgbChannel(value.GetZ()));
    }

    Vector3 ReinhardToneMap(const Vector3& hdrColor) noexcept
    {
        return Vector3(hdrColor.GetX() / (1.0f + hdrColor.GetX()), hdrColor.GetY() / (1.0f + hdrColor.GetY()), hdrColor.GetZ() / (1.0f + hdrColor.GetZ()));
    }

    Vector3 AcesToneMap(const Vector3& hdrColor, Float32 whitePoint) noexcept
    {
        const Vector3 color = hdrColor / std::max(whitePoint, MinDenominator);
        const Vector3 numerator = ComponentMultiply(color, color * 2.51f + Vector3(0.03f, 0.03f, 0.03f));
        const Vector3 denominator = ComponentMultiply(color, color * 2.43f + Vector3(0.59f, 0.59f, 0.59f)) + Vector3(0.14f, 0.14f, 0.14f);
        return Saturate(Vector3(numerator.GetX() / denominator.GetX(), numerator.GetY() / denominator.GetY(), numerator.GetZ() / denominator.GetZ()));
    }

    Vector3 ApplyToneMapping(const Vector3& hdrColor, Float32 exposure, ToneMappingMode mode, Float32 whitePoint) noexcept
    {
        const Vector3 exposed = hdrColor * std::pow(2.0f, exposure);
        const Vector3 mapped = mode == ToneMappingMode::Reinhard ? ReinhardToneMap(exposed) : AcesToneMap(exposed, whitePoint);
        return LinearToSrgb(mapped);
    }

    Float32 DistributionGgx(Float32 nDotH, Float32 roughness) noexcept
    {
        const Float32 alpha = std::max(roughness * roughness, 0.002025f);
        const Float32 alpha2 = alpha * alpha;
        const Float32 nDotH2 = Saturate(nDotH) * Saturate(nDotH);
        const Float32 denominator = Pi * std::pow(nDotH2 * (alpha2 - 1.0f) + 1.0f, 2.0f);
        return alpha2 / std::max(denominator, MinDenominator);
    }

    Float32 GeometrySchlickGgx(Float32 nDotV, Float32 roughness) noexcept
    {
        const Float32 k = (roughness + 1.0f) * (roughness + 1.0f) / 8.0f;
        return Saturate(nDotV) / std::max(Saturate(nDotV) * (1.0f - k) + k, MinDenominator);
    }

    Float32 GeometrySmith(Float32 nDotV, Float32 nDotL, Float32 roughness) noexcept
    {
        return GeometrySchlickGgx(nDotV, roughness) * GeometrySchlickGgx(nDotL, roughness);
    }

    Vector3 FresnelSchlick(Float32 cosTheta, const Vector3& f0) noexcept
    {
        const Float32 factor = Pow5(1.0f - Saturate(cosTheta));
        return f0 + (Vector3::One() - f0) * factor;
    }

    Vector3 FresnelSchlickRoughness(Float32 cosTheta, const Vector3& f0, Float32 roughness) noexcept
    {
        const Vector3 oneMinusRoughness(1.0f - roughness, 1.0f - roughness, 1.0f - roughness);
        const Vector3 reflectance = Vector3(std::max(oneMinusRoughness.GetX(), f0.GetX()),
                                             std::max(oneMinusRoughness.GetY(), f0.GetY()),
                                             std::max(oneMinusRoughness.GetZ(), f0.GetZ()));
        return reflectance + (Vector3::One() - reflectance) * Pow5(1.0f - Saturate(cosTheta));
    }

    Vector3 EvaluateDirectionalLight(const Vector3& normal,
                                     const Vector3& viewDirection,
                                     const Vector3& lightDirection,
                                     const Vector3& baseColor,
                                     Float32 metallic,
                                     Float32 roughness,
                                     const Vector3& lightColor,
                                     Float32 lightIntensity) noexcept
    {
        const Vector3 n = normal.Normalized();
        const Vector3 v = viewDirection.Normalized();
        const Vector3 l = lightDirection.Normalized();
        const Vector3 h = (v + l).Normalized();
        const Float32 nDotL = Saturate(Vector3::Dot(n, l));
        const Float32 nDotV = Saturate(Vector3::Dot(n, v));
        const Float32 nDotH = Saturate(Vector3::Dot(n, h));
        const Float32 vDotH = Saturate(Vector3::Dot(v, h));
        const Float32 clampedMetallic = Saturate(metallic);
        const Float32 clampedRoughness = std::clamp(roughness, 0.045f, 1.0f);
        const Vector3 f0 = Vector3(0.04f, 0.04f, 0.04f) * (1.0f - clampedMetallic) + baseColor * clampedMetallic;
        const Vector3 fresnel = FresnelSchlick(vDotH, f0);
        const Float32 normalDistribution = DistributionGgx(nDotH, clampedRoughness);
        const Float32 geometry = GeometrySmith(nDotV, nDotL, clampedRoughness);
        const Vector3 specular = fresnel * (normalDistribution * geometry / std::max(4.0f * nDotV * nDotL, MinDenominator));
        const Vector3 diffuse = baseColor * (1.0f / Pi);
        const Vector3 diffuseWeight = (Vector3::One() - fresnel) * (1.0f - clampedMetallic);
        return ComponentMultiply(ComponentMultiply(diffuse, diffuseWeight) + specular, lightColor) * (std::max(lightIntensity, 0.0f) * nDotL);
    }
} // namespace ve::pbr
