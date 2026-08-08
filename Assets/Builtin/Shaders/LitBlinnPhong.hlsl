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
    VirtualShadowClipmapConstants virtualShadowClipmaps[24];
    uint virtualShadowAtlasExtent;
    uint virtualShadowPhysicalPageSize;
    uint virtualShadowClipmapLevelCount;
    uint virtualShadowAtlasPadding;
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
    uint virtualShadowViewID;
    uint3 virtualShadowViewIDPadding;
};

Texture2D<uint> VirtualShadowAtlas : register(t1, space0);
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

float SampleVirtualShadowPage(uint physicalPageIndex, float2 pagePosition, float receiverDepth)
{
    uint pagesPerRow = virtualShadowAtlasExtent / virtualShadowPhysicalPageSize;
    uint2 physicalPage = uint2(physicalPageIndex % pagesPerRow, physicalPageIndex / pagesPerRow);
    uint2 pageOrigin = physicalPage * virtualShadowPhysicalPageSize;
    const float texelSize = 1.0f / virtualShadowPhysicalPageSize;
    const float2 minimumPagePosition = float2(0.5f, 0.5f) * texelSize;
    const float2 maximumPagePosition = 1.0f - minimumPagePosition;
    const float2 centerPosition = clamp(pagePosition, minimumPagePosition, maximumPagePosition);
    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 samplePosition = clamp(centerPosition + float2(x, y) * texelSize,
                                                 minimumPagePosition,
                                                 maximumPagePosition);
            const uint2 pagePixel = min(uint2(samplePosition * virtualShadowPhysicalPageSize),
                                        uint2(virtualShadowPhysicalPageSize - 1u, virtualShadowPhysicalPageSize - 1u));
            const uint encodedDepth = VirtualShadowAtlas.Load(int3(pageOrigin + pagePixel, 0));
            if (encodedDepth == 0u || receiverDepth <= 1.0f - asfloat(encodedDepth))
            {
                visibility += 1.0f;
            }
        }
    }
    return visibility / 9.0f;
}

bool TryResolveVirtualShadowPage(float3 worldPosition,
                                 float3 worldNormal,
                                 out uint resolvedLevel,
                                 out int2 resolvedPageCoordinate,
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
        if (pageIndex == 0xFFFFFFFFu)
        {
            continue;
        }

        float depthRange = clipmap.radiusAndDepthRange.z - clipmap.radiusAndDepthRange.y;
        float resolvedDepthReference =
            (lightPosition.z - clipmap.radiusAndDepthRange.y) / depthRange - virtualShadowAtlasAndBias.y;
        if (resolvedDepthReference < 0.0f || resolvedDepthReference > 1.0f)
        {
            continue;
        }

        resolvedLevel = sampleLevel;
        resolvedPageCoordinate = pageCoordinate;
        physicalPageIndex = pageIndex;
        pagePosition = lightPosition.xy / pageWorldSize - float2(pageCoordinate);
        depthReference = resolvedDepthReference;
        return true;
    }

    resolvedLevel = 0u;
    resolvedPageCoordinate = int2(0, 0);
    physicalPageIndex = 0xFFFFFFFFu;
    pagePosition = float2(0.0f, 0.0f);
    depthReference = 0.0f;
    return false;
}

float ComputeVirtualShadowVisibility(float3 worldPosition, float3 worldNormal, uint objectReceivesShadows)
{
    if (objectReceivesShadows == 0u)
    {
        return 1.0f;
    }

    uint resolvedLevel;
    int2 resolvedPageCoordinate;
    uint physicalPageIndex;
    float2 pagePosition;
    float depthReference;
    if (!TryResolveVirtualShadowPage(
            worldPosition, worldNormal, resolvedLevel, resolvedPageCoordinate, physicalPageIndex, pagePosition, depthReference))
    {
        return 1.0f;
    }

    return SampleVirtualShadowPage(physicalPageIndex, pagePosition, depthReference);
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
