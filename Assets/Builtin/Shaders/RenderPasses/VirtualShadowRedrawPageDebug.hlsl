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

struct PhysicalPage
{
    uint key0;
    uint key1;
    uint lastUsedFrame;
    uint lastRenderedFrame;
    uint flags;
    uint3 padding;
};

StructuredBuffer<PhysicalPage> PhysicalPages : register(t6);

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

bool TryResolveVirtualShadowDebugPage(float3 worldPosition,
                                 float3 worldNormal,
                                 out uint physicalPageIndex,
                                 out float2 pagePosition,
                                 out float depthReference)
{
    float cameraDepth = max(dot(worldPosition - cameraWorldPosition.xyz, cameraWorldForward.xyz), 0.0f);
    uint firstLevel = virtualShadowClipmapLevelCount - 1u;
    [loop]
    for (uint levelIndex = 0u; levelIndex < virtualShadowClipmapLevelCount; ++levelIndex)
    {
        if (cameraDepth <= virtualShadowClipmaps[levelIndex].radiusAndDepthRange.x)
        {
            firstLevel = levelIndex;
            break;
        }
    }

    float3 biasedWorldPosition = worldPosition + normalize(worldNormal) * virtualShadowAtlasAndBias.z;
    float3 lightPosition = float3(dot(biasedWorldPosition, virtualShadowLightRight.xyz),
                                  dot(biasedWorldPosition, virtualShadowLightUp.xyz),
                                  dot(biasedWorldPosition, virtualShadowLightDirection.xyz));
    [loop]
    for (uint sampleLevel = firstLevel; sampleLevel < virtualShadowClipmapLevelCount; ++sampleLevel)
    {
        VirtualShadowClipmapConstants clipmap = virtualShadowClipmaps[sampleLevel];
        float pageWorldSize = clipmap.lightOriginAndPageWorldSize.w;
        int2 pageCoordinate = int2(floor(lightPosition.xy / pageWorldSize));
        uint pageIndex = FindVirtualShadowPhysicalPage(sampleLevel, pageCoordinate);
        if (pageIndex == 0xFFFFFFFFu || pageIndex >= virtualShadowPhysicalPageCapacity)
        {
            continue;
        }

        float depthRange = clipmap.radiusAndDepthRange.z - clipmap.radiusAndDepthRange.y;
        float resolvedDepthReference = (lightPosition.z - clipmap.radiusAndDepthRange.y) / depthRange - virtualShadowAtlasAndBias.y;
        if (resolvedDepthReference < 0.0f || resolvedDepthReference > 1.0f)
        {
            continue;
        }

        physicalPageIndex = pageIndex;
        pagePosition = lightPosition.xy / pageWorldSize - float2(pageCoordinate);
        depthReference = resolvedDepthReference;
        return true;
    }

    physicalPageIndex = 0xFFFFFFFFu;
    pagePosition = float2(0.0f, 0.0f);
    depthReference = 0.0f;
    return false;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 normal = normalize(input.worldNormal);
    uint physicalPageIndex;
    float2 pagePosition;
    float depthReference;
    bool pageResolved = TryResolveVirtualShadowDebugPage(input.worldPosition, normal, physicalPageIndex, pagePosition, depthReference);

    float shadowVisibility = 1.0f;
    float3 redrawTint = float3(0.45f, 0.45f, 0.45f);
    if (pageResolved)
    {
        PhysicalPage page = PhysicalPages[physicalPageIndex];
        if (input.receiveShadows != 0u)
        {
            shadowVisibility = SampleVirtualShadowPage(physicalPageIndex, pagePosition, depthReference);
        }

        uint age = virtualShadowFrameIndex - page.lastRenderedFrame;
        float ageFactor = saturate(float(age) / float(60.0f));
        redrawTint = lerp(float3(1.0f, 0.04f, 0.02f), float3(0.04f, 0.9f, 0.12f), ageFactor);
    }

    float3 lightDirection = normalize(directionalLightDirection.xyz);
    float diffuse = saturate(dot(normal, -lightDirection));
    float3 directLight = directionalLightColorAndIntensity.rgb * directionalLightColorAndIntensity.w * diffuse * shadowVisibility;
    float3 lighting = ambientColor.rgb + directLight;
    float3 debugBaseColor = lerp(float3(0.25f, 0.25f, 0.25f), redrawTint, 0.8f);
    return float4(saturate(debugBaseColor * lighting), 1.0f);
}
