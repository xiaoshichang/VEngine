cbuffer FrameConstants : register(b0, space0)
{
    float4 directionalLightDirection;
    float4 directionalLightColorAndIntensity;
    float4 ambientColor;
};

cbuffer ViewConstants : register(b1, space0)
{
    float4x4 viewProjection;
    float4 cameraWorldPosition;
    float4 cameraWorldForward;
};

cbuffer ObjectConstants : register(b2, space0)
{
    float4x4 localToWorld;
    uint receiveShadows;
    uint3 objectPadding;
};

cbuffer MaterialConstants : register(b3, space0)
{
    float4 baseColor;
    float metallic;
    float roughness;
    float normalScale;
    float materialPadding;
    float4 emissive;
};

#include <VEngine/VirtualShadowSampling.hlsli>

static const float Pi = 3.14159265359f;

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL0;
    float3 worldPosition : TEXCOORD0;
    nointerpolation uint receivesShadows : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float4 worldPosition = mul(localToWorld, float4(input.position, 1.0f));
    output.position = mul(viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(mul((float3x3)localToWorld, input.normal));
    output.receivesShadows = receiveShadows;
    return output;
}

float DistributionGgx(float nDotH, float surfaceRoughness)
{
    const float alpha = max(surfaceRoughness * surfaceRoughness, 0.002025f);
    const float alphaSquared = alpha * alpha;
    const float denominatorTerm = nDotH * nDotH * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(Pi * denominatorTerm * denominatorTerm, 0.0001f);
}

float GeometrySchlickGgx(float nDotV, float surfaceRoughness)
{
    const float k = (surfaceRoughness + 1.0f) * (surfaceRoughness + 1.0f) / 8.0f;
    return nDotV / max(nDotV * (1.0f - k) + k, 0.0001f);
}

float GeometrySmith(float nDotV, float nDotL, float surfaceRoughness)
{
    return GeometrySchlickGgx(nDotV, surfaceRoughness) * GeometrySchlickGgx(nDotL, surfaceRoughness);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - saturate(cosTheta), 5.0f);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    const float3 normal = normalize(input.worldNormal);
    const float3 viewDirection = normalize(cameraWorldPosition.xyz - input.worldPosition);
    const float3 lightDirection = normalize(-directionalLightDirection.xyz);
    const float3 halfDirection = normalize(viewDirection + lightDirection);
    const float nDotL = saturate(dot(normal, lightDirection));
    const float nDotV = saturate(dot(normal, viewDirection));
    const float nDotH = saturate(dot(normal, halfDirection));
    const float vDotH = saturate(dot(viewDirection, halfDirection));
    const float clampedMetallic = saturate(metallic);
    const float clampedRoughness = clamp(roughness, 0.045f, 1.0f);
    const float3 albedo = max(baseColor.rgb, 0.0f);
    const float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, clampedMetallic);
    const float3 fresnel = FresnelSchlick(vDotH, f0);
    const float normalDistribution = DistributionGgx(nDotH, clampedRoughness);
    const float geometry = GeometrySmith(nDotV, nDotL, clampedRoughness);
    const float3 specular = normalDistribution * geometry * fresnel / max(4.0f * nDotV * nDotL, 0.0001f);
    const float3 diffuseWeight = (1.0f - fresnel) * (1.0f - clampedMetallic);
    const float3 diffuse = diffuseWeight * albedo / Pi;
    const float shadowVisibility = ComputeVirtualShadowVisibility(input.worldPosition, normal, input.receivesShadows);
    const float3 radiance = directionalLightColorAndIntensity.rgb * max(directionalLightColorAndIntensity.a, 0.0f) * shadowVisibility;
    const float3 directLighting = (diffuse + specular) * radiance * nDotL;
    const float3 ambientLighting = ambientColor.rgb * diffuseWeight * albedo;
    return float4(directLighting + ambientLighting + max(emissive.rgb, 0.0f), baseColor.a);
}
