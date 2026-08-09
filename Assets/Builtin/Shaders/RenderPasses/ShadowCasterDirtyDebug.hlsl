cbuffer FrameConstants : register(b0)
{
    float4 directionalLightDirection;
    float4 directionalLightColorAndIntensity;
    float4 ambientColor;
};

cbuffer ViewConstants : register(b1)
{
    float4x4 viewProjection;
    float4 cameraWorldPosition;
    float4 cameraWorldForward;
};

cbuffer ObjectConstants : register(b2)
{
    float4x4 localToWorld;
    uint receiveShadows;
    uint3 objectPadding;
};

#include <VEngine/VirtualShadowSampling.hlsli>

cbuffer DebugObjectConstants : register(b6)
{
    uint shadowCasterDirty;
    uint3 debugObjectPadding;
};

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
    nointerpolation uint receiveShadows : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPosition = mul(localToWorld, float4(input.position, 1.0f));
    output.position = mul(viewProjection, worldPosition);
    output.worldNormal = mul((float3x3)localToWorld, input.normal);
    output.worldPosition = worldPosition.xyz;
    output.receiveShadows = receiveShadows;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 normal = normalize(input.worldNormal);
    float3 lightDirection = normalize(directionalLightDirection.xyz);
    float diffuse = saturate(dot(normal, -lightDirection));
    float shadowVisibility = ComputeVirtualShadowVisibility(input.worldPosition, normal, input.receiveShadows);
    float3 directLight = directionalLightColorAndIntensity.rgb * directionalLightColorAndIntensity.w * diffuse * shadowVisibility;
    float3 lighting = ambientColor.rgb + directLight;
    float3 debugColor = shadowCasterDirty != 0u ? float3(1.0f, 0.04f, 0.02f) : float3(0.04f, 0.9f, 0.12f);
    float3 debugBaseColor = lerp(float3(0.25f, 0.25f, 0.25f), debugColor, 0.8f);
    return float4(saturate(debugBaseColor * lighting), 1.0f);
}
