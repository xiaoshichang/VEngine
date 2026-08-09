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
};

Texture2D MainTexture : register(t0, space0);

#include <VEngine/VirtualShadowSampling.hlsli>

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
    float3 lightToSurface = -lightDirection;
    float3 surfaceToCamera = normalize(cameraWorldPosition.xyz - input.worldPosition);
    float3 halfDirection = normalize(lightToSurface + surfaceToCamera);
    float diffuse = saturate(dot(normal, lightToSurface));
    float specular = pow(saturate(dot(normal, halfDirection)), 32.0f);
    float shadowVisibility = ComputeVirtualShadowVisibility(input.worldPosition, normal, input.receiveShadows);
    float3 lightColor = directionalLightColorAndIntensity.rgb;
    float3 directLight = lightColor * directionalLightColorAndIntensity.w * shadowVisibility;
    float3 litColor = baseColor.rgb * (ambientColor.rgb + directLight * diffuse) + directLight * specular * 0.25f;
    return float4(saturate(litColor), baseColor.a);
}
