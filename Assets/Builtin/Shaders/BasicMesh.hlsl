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

struct VirtualShadowClipmapConstants
{
    float4 lightOriginAndPageWorldSize;
    float4 radiusAndDepthRange;
    int4 pageData;
};

struct VirtualShadowInvalidationEntry
{
    uint4 data;
};

cbuffer VirtualShadowConstants : register(b4, space0)
{
    float4 virtualShadowLightRight;
    float4 virtualShadowLightUp;
    float4 virtualShadowLightDirection;
    float4 virtualShadowAtlasAndBias;
    VirtualShadowClipmapConstants virtualShadowClipmaps[4];
    uint virtualShadowEnabled;
    uint virtualShadowAtlasExtent;
    uint virtualShadowPhysicalPageSize;
    uint virtualShadowClipmapLevelCount;
    float4x4 virtualShadowInverseViewProjection;
    uint virtualShadowScreenWidth;
    uint virtualShadowScreenHeight;
    uint virtualShadowPhysicalPageCapacity;
    uint virtualShadowFrameIndex;
    uint virtualShadowResetCache;
    uint virtualShadowPassLevel;
    uint virtualShadowInvalidationCount;
    uint virtualShadowPadding;
    float4 virtualShadowCameraWorldPosition;
    float4 virtualShadowCameraWorldForward;
    VirtualShadowInvalidationEntry virtualShadowInvalidationEntries[2048];
};

Texture2D<float> VirtualShadowAtlas : register(t1, space0);
SamplerComparisonState VirtualShadowSampler : register(s1, space0);
StructuredBuffer<uint> VirtualShadowDensePageTable : register(t5, space0);

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

uint FindVirtualShadowPhysicalPage(uint level, int2 pageCoordinate)
{
    int2 localPage = pageCoordinate - (virtualShadowClipmaps[level].pageData.xy - int2(64, 64));
    if (any(localPage < 0) || any(localPage >= 128))
    {
        return 0xFFFFFFFFu;
    }
    uint logicalIndex = level * 16384u + uint(localPage.y) * 128u + uint(localPage.x);
    uint denseEntry = VirtualShadowDensePageTable[logicalIndex];
    return denseEntry == 0u ? 0xFFFFFFFFu : denseEntry - 1u;
}

float SampleVirtualShadowPage(uint physicalPageIndex, float2 pagePosition, float depthReference)
{
    const float contentSize = 126.0f;
    uint pagesPerRow = virtualShadowAtlasExtent / virtualShadowPhysicalPageSize;
    uint2 physicalPage = uint2(physicalPageIndex % pagesPerRow, physicalPageIndex / pagesPerRow);
    float2 contentOrigin = float2(physicalPage * virtualShadowPhysicalPageSize) + 1.0f;
    float2 minimumPixel = contentOrigin + 0.5f;
    float2 maximumPixel = contentOrigin + contentSize - 0.5f;
    float2 samplePagePosition = float2(pagePosition.x, 1.0f - pagePosition.y);
    float2 centerPixel = minimumPixel + saturate(samplePagePosition) * (contentSize - 1.0f);

    float visibility = 0.0f;
    [unroll]
    for (int offsetY = -1; offsetY <= 1; ++offsetY)
    {
        [unroll]
        for (int offsetX = -1; offsetX <= 1; ++offsetX)
        {
            float2 samplePixel = clamp(centerPixel + float2(offsetX, offsetY), minimumPixel, maximumPixel);
            visibility += VirtualShadowAtlas.SampleCmpLevelZero(
                VirtualShadowSampler, samplePixel * virtualShadowAtlasAndBias.x, depthReference);
        }
    }
    return visibility / 9.0f;
}

float ComputeVirtualShadowVisibility(float3 worldPosition, float3 worldNormal, uint objectReceivesShadows)
{
    if (virtualShadowEnabled == 0u || objectReceivesShadows == 0u || virtualShadowAtlasExtent == 0u)
    {
        return 1.0f;
    }

    float cameraDepth = max(dot(worldPosition - cameraWorldPosition.xyz, cameraWorldForward.xyz), 0.0f);
    uint firstLevel = virtualShadowClipmapLevelCount - 1u;
    [unroll]
    for (uint levelIndex = 0u; levelIndex < 4u; ++levelIndex)
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
        uint physicalPageIndex = FindVirtualShadowPhysicalPage(sampleLevel, pageCoordinate);
        if (physicalPageIndex == 0xFFFFFFFFu)
        {
            continue;
        }

        float depthRange = clipmap.radiusAndDepthRange.z - clipmap.radiusAndDepthRange.y;
        float depthReference = (lightPosition.z - clipmap.radiusAndDepthRange.y) / depthRange - virtualShadowAtlasAndBias.y;
        if (depthReference < 0.0f || depthReference > 1.0f)
        {
            continue;
        }

        float2 pagePosition = lightPosition.xy / pageWorldSize - float2(pageCoordinate);
        return SampleVirtualShadowPage(physicalPageIndex, pagePosition, depthReference);
    }

    return 1.0f;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 normal = normalize(input.worldNormal);
    float3 lightDirection = normalize(directionalLightDirection.xyz);
    float3 lightToSurface = -lightDirection;
    float diffuse = saturate(dot(normal, lightToSurface));
    float shadowVisibility = ComputeVirtualShadowVisibility(input.worldPosition, normal, input.receiveShadows);
    float3 lightColor = directionalLightColorAndIntensity.rgb;
    float3 litColor = baseColor.rgb * (ambientColor.rgb + (lightColor * directionalLightColorAndIntensity.w * diffuse * shadowVisibility));
    return float4(saturate(litColor), baseColor.a);
}
