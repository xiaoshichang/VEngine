#include <VEngine/VirtualShadowCommon.hlsli>

#if defined(VE_VSM_STEP_1)
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u0);
RWStructuredBuffer<uint> Statistics : register(u1);

[numthreads(64, 1, 1)]
void VirtualShadowStep1_ClearSceneCS(uint index : SV_DispatchThreadID)
{
    if (index < 6u)
    {
        Statistics[index] = 0u;
    }
    if (index >= physicalCapacity)
    {
        return;
    }
    PhysicalPage page = PhysicalPages[index];
    if (resetCache != 0u)
    {
        page.key0 = 0xFFFFFFFFu;
        page.key1 = 0xFFFFFFFFu;
        page.lastUsedFrame = 0u;
        page.lastRenderedFrame = 0u;
        page.flags = 0u;
    }
    else
    {
        page.flags &= ~12u;
    }
    PhysicalPages[index] = page;
}
#endif

#if defined(VE_VSM_STEP_2)
RWStructuredBuffer<uint> PageMarks : register(u0);
RWStructuredBuffer<uint> RequestCounts : register(u1);
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u2);

[numthreads(64, 1, 1)]
void VirtualShadowStep2_ClearViewRequestsCS(uint index : SV_DispatchThreadID)
{
    if (resetCache != 0u && index < clipmapCount * 16384u)
    {
        PageMarks[index] = 0u;
    }
    if (index < clipmapCount)
    {
        RequestCounts[index] = 0u;
    }
    if (index >= physicalCapacity)
    {
        return;
    }

    PhysicalPage page = PhysicalPages[index];
    if ((page.flags & 1u) == 0u)
    {
        return;
    }
    bool invalidated = invalidationCount == 0xFFFFFFFFu && (((page.key1 >> 8u) & 0x00FFFFFFu) == (viewID & 0x00FFFFFFu));
    [loop]
    for (uint invalidationIndex = 0u; !invalidated && invalidationIndex < min(invalidationCount, 2048u); ++invalidationIndex)
    {
        invalidated = invalidatedPages[invalidationIndex].data.x == page.key0 && invalidatedPages[invalidationIndex].data.y == page.key1;
    }
    if (invalidated)
    {
        page.flags |= 2u;
        PhysicalPages[index] = page;
    }
}
#endif

#if defined(VE_VSM_STEP_3)
Texture2D<float> SceneDepth : register(t0);
RWStructuredBuffer<uint> PageMarks : register(u0);
RWStructuredBuffer<uint> RequestList : register(u1);
RWStructuredBuffer<uint> RequestCounts : register(u2);
RWStructuredBuffer<uint> Statistics : register(u3);

uint SelectClipmapLevel(float cameraDepth)
{
    uint preferredLevel = clipmapCount - 1u;
    [loop]
    for (uint level = 0u; level < clipmapCount; ++level)
    {
        if (cameraDepth <= clipmaps[level].radiusAndDepth.x)
        {
            preferredLevel = level;
            break;
        }
    }
    return preferredLevel;
}

bool IsPositionCoveredByClipmap(uint level, float2 lightPosition, out int2 localPage)
{
    float pageSize = clipmaps[level].originAndPageSize.w;
    int2 page = int2(floor(lightPosition / pageSize));
    int2 minimumPage = clipmaps[level].pageData.xy - int2(64, 64);
    localPage = page - minimumPage;
    return all(localPage >= 0) && all(localPage < 128);
}

void RequestPage(uint level, int2 localPage)
{
    uint logical = level * 16384u + uint(localPage.y) * 128u + uint(localPage.x);
    uint previousGeneration = 0u;
    uint generation = frameIndex + 1u;
    InterlockedExchange(PageMarks[logical], generation, previousGeneration);
    if (previousGeneration == generation)
    {
        return;
    }

    uint requestIndex = 0u;
    InterlockedAdd(RequestCounts[level], 1u, requestIndex);
    if (requestIndex < 16384u)
    {
        RequestList[level * 16384u + requestIndex] = logical;
        InterlockedAdd(Statistics[1], 1u);
    }
}

[numthreads(8, 8, 1)]
void VirtualShadowStep3_MarkRequestsCS(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= screenWidth || id.y >= screenHeight)
    {
        return;
    }
    float depth = SceneDepth.Load(int3(id.xy, 0));
    if (depth >= 1.0f)
    {
        return;
    }
    float2 uv = (float2(id.xy) + 0.5f) / float2(screenWidth, screenHeight);
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 world = mul(inverseViewProjection, clip);
    if (abs(world.w) < 1.0e-6f)
    {
        return;
    }
    world.xyz /= world.w;
    float2 lightPosition = float2(dot(world.xyz, lightRight.xyz), dot(world.xyz, lightUp.xyz));
    float cameraDepth = max(dot(world.xyz - cameraWorldPosition.xyz, cameraWorldForward.xyz), 0.0f);
    uint preferredLevel = SelectClipmapLevel(cameraDepth);
    [loop]
    for (uint level = preferredLevel; level < clipmapCount; ++level)
    {
        int2 localPage = int2(0, 0);
        if (!IsPositionCoveredByClipmap(level, lightPosition, localPage))
        {
            continue;
        }
        RequestPage(level, localPage);
        break;
    }
}
#endif

#if defined(VE_VSM_STEP_4)
StructuredBuffer<uint> RequestList : register(t0);
StructuredBuffer<uint> RequestCounts : register(t1);
RWStructuredBuffer<uint> PageTable : register(u0);
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u1);
RWStructuredBuffer<uint> Statistics : register(u2);

[numthreads(64, 1, 1)]
void VirtualShadowStep4_ResolvePageHitsCS(uint requestIndex : SV_DispatchThreadID)
{
    if (requestIndex >= 16384u)
    {
        return;
    }
    for (uint level = 0u; level < clipmapCount; ++level)
    {
        if (requestIndex >= min(RequestCounts[level], 16384u))
        {
            continue;
        }
        if (passLevel != 0u)
        {
            InterlockedAdd(Statistics[1], 1u);
        }

        uint logical = RequestList[level * 16384u + requestIndex];
        uint levelIndex = logical - level * 16384u;
        int2 localPage = int2(levelIndex & 127u, levelIndex >> 7u);
        int2 absolutePage = clipmaps[level].pageData.xy - int2(64, 64) + localPage;
        uint key0 = PackVirtualShadowPageKey(absolutePage);
        uint key1 = PackVirtualShadowViewLevelKey(level);

        uint mappedPhysicalPlusOne = PageTable[logical];
        if (mappedPhysicalPlusOne != 0u && mappedPhysicalPlusOne <= physicalCapacity)
        {
            uint mappedPhysical = mappedPhysicalPlusOne - 1u;
            PhysicalPage mappedPage = PhysicalPages[mappedPhysical];
            if ((mappedPage.flags & 1u) != 0u && mappedPage.key0 == key0 && mappedPage.key1 == key1)
            {
                InterlockedMax(PhysicalPages[mappedPhysical].lastUsedFrame, frameIndex);
                InterlockedOr(PhysicalPages[mappedPhysical].flags, 4u);
                InterlockedAdd(Statistics[2], 1u);
                continue;
            }
        }
        PageTable[logical] = 0u;

        for (uint physical = 0u; physical < physicalCapacity; ++physical)
        {
            PhysicalPage page = PhysicalPages[physical];
            if ((page.flags & 1u) != 0u && page.key0 == key0 && page.key1 == key1)
            {
                InterlockedMax(PhysicalPages[physical].lastUsedFrame, frameIndex);
                InterlockedOr(PhysicalPages[physical].flags, 4u);
                PageTable[logical] = physical + 1u;
                InterlockedAdd(Statistics[2], 1u);
                break;
            }
        }
    }
}
#endif

#if defined(VE_VSM_STEP_5)
StructuredBuffer<uint> RequestList : register(t0);
StructuredBuffer<uint> RequestCounts : register(t1);
RWStructuredBuffer<uint> PageTable : register(u0);
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u1);
RWStructuredBuffer<uint> Statistics : register(u2);

[numthreads(1, 1, 1)]
void VirtualShadowStep5_AllocatePagesCS(uint3 id : SV_DispatchThreadID)
{
    for (uint coarseIteration = 0u; coarseIteration < clipmapCount; ++coarseIteration)
    {
        uint level = clipmapCount - 1u - coarseIteration;
        uint requestCount = min(RequestCounts[level], 16384u);
        for (uint requestIndex = 0u; requestIndex < requestCount; ++requestIndex)
        {
            uint logical = RequestList[level * 16384u + requestIndex];
            if (PageTable[logical] != 0u)
            {
                continue;
            }
            uint localIndex = logical - level * 16384u;
            int2 localPage = int2(localIndex & 127u, localIndex >> 7u);
            int2 absolutePage = clipmaps[level].pageData.xy - int2(64, 64) + localPage;
            uint key0 = PackVirtualShadowPageKey(absolutePage);
            uint key1 = PackVirtualShadowViewLevelKey(level);

            uint selected = 0xFFFFFFFFu;
            uint oldestFrame = 0xFFFFFFFFu;
            uint oldestPage = 0xFFFFFFFFu;
            for (uint physical = 0u; physical < physicalCapacity; ++physical)
            {
                PhysicalPage page = PhysicalPages[physical];
                if ((page.flags & 4u) != 0u)
                {
                    continue;
                }
                if ((page.flags & 1u) != 0u && page.key0 == key0 && page.key1 == key1)
                {
                    selected = physical;
                    break;
                }
                if ((page.flags & 1u) == 0u && selected == 0xFFFFFFFFu)
                {
                    selected = physical;
                }
                if ((page.flags & 1u) != 0u && page.lastUsedFrame < oldestFrame)
                {
                    oldestFrame = page.lastUsedFrame;
                    oldestPage = physical;
                }
            }
            if (selected == 0xFFFFFFFFu)
            {
                selected = oldestPage;
            }
            if (selected == 0xFFFFFFFFu)
            {
                InterlockedAdd(Statistics[5], 1u);
                continue;
            }

            PhysicalPage selectedPage = PhysicalPages[selected];
            bool cacheHit = (selectedPage.flags & 1u) != 0u && selectedPage.key0 == key0 && selectedPage.key1 == key1;
            InterlockedAdd(Statistics[cacheHit ? 2u : 3u], 1u);
            selectedPage.key0 = key0;
            selectedPage.key1 = key1;
            selectedPage.lastUsedFrame = frameIndex;
            if (!cacheHit)
            {
                selectedPage.lastRenderedFrame = 0u;
            }
            selectedPage.flags = 1u | 4u | (cacheHit ? (selectedPage.flags & 2u) : 2u);
            PhysicalPages[selected] = selectedPage;
            PageTable[logical] = selected + 1u;
        }
    }
}
#endif

#if defined(VE_VSM_STEP_6)
StructuredBuffer<PhysicalPage> PhysicalPages : register(t2);
RWTexture2D<uint> PhysicalAtlas : register(u0);

[numthreads(8, 8, 1)]
void VirtualShadowStep6_ClearPhysicalPagesCS(uint3 groupID : SV_GroupID, uint3 groupThreadID : SV_GroupThreadID)
{
    uint physicalIndex = groupID.x;
    if (physicalIndex >= physicalCapacity)
    {
        return;
    }

    PhysicalPage page = PhysicalPages[physicalIndex];
    bool matchesView = IsVirtualShadowPhysicalPageForView(page);
    if ((page.flags & 7u) != 7u || !matchesView)
    {
        return;
    }

    uint pagesPerRow = atlasExtent / physicalPageSize;
    uint2 slotOrigin = uint2(physicalIndex % pagesPerRow, physicalIndex / pagesPerRow) * physicalPageSize;
    for (uint y = groupThreadID.y; y < physicalPageSize; y += 8u)
    {
        for (uint x = groupThreadID.x; x < physicalPageSize; x += 8u)
        {
            PhysicalAtlas[slotOrigin + uint2(x, y)] = 0u;
        }
    }
}
#endif

#if defined(VE_VSM_STEP_7)
cbuffer ObjectConstants : register(b2)
{
    float4x4 localToWorld;
    uint receiveShadows;
    uint3 objectPadding;
};

StructuredBuffer<uint> PageTable : register(t5);
StructuredBuffer<PhysicalPage> PhysicalPages : register(t2);
RWTexture2D<uint> PhysicalAtlas : register(u0);

struct Input
{
    float3 position : POSITION;
};

struct Output
{
    float4 position : SV_POSITION;
    nointerpolation uint level : TEXCOORD0;
    float normalizedDepth : TEXCOORD1;
};

Output VirtualShadowStep7_RenderCastersVS(Input input, uint instanceID : SV_InstanceID)
{
    Output output;
    uint level = instanceID;
    float4 world = mul(localToWorld, float4(input.position, 1.0f));
    float3 light = float3(dot(world.xyz, lightRight.xyz), dot(world.xyz, lightUp.xyz), dot(world.xyz, lightForward.xyz));

    float pageWorldSize = clipmaps[level].originAndPageSize.w;
    float2 workingRegionMinimum = float2(clipmaps[level].pageData.xy - int2(64, 64)) * pageWorldSize;
    float2 virtualUv = (light.xy - workingRegionMinimum) / (pageWorldSize * 128.0f);

    float depthRange = clipmaps[level].radiusAndDepth.z - clipmaps[level].radiusAndDepth.y;
    output.position = float4(virtualUv.x * 2.0f - 1.0f, 1.0f - virtualUv.y * 2.0f, 0.0f, 1.0f);
    output.level = level;
    output.normalizedDepth = (light.z - clipmaps[level].radiusAndDepth.y) / depthRange;
    return output;
}

void VirtualShadowStep7_RenderCastersPS(Output input)
{
    if (input.level >= clipmapCount || input.normalizedDepth < 0.0f || input.normalizedDepth > 1.0f)
    {
        discard;
    }

    uint2 virtualPixel = uint2(input.position.xy);
    if (any(virtualPixel >= uint2(16384u, 16384u)))
    {
        discard;
    }

    uint2 localPage = virtualPixel / physicalPageSize;
    uint2 pagePixel = virtualPixel % physicalPageSize;
    int2 absolutePage = clipmaps[input.level].pageData.xy - int2(64, 64) + int2(localPage);
    uint expectedKey0 = PackVirtualShadowPageKey(absolutePage);
    uint logicalIndex = input.level * 16384u + localPage.y * 128u + localPage.x;
    uint denseEntry = PageTable[logicalIndex];
    if (denseEntry == 0u)
    {
        discard;
    }

    uint physicalIndex = denseEntry - 1u;
    if (physicalIndex >= physicalCapacity)
    {
        discard;
    }

    PhysicalPage page = PhysicalPages[physicalIndex];
    bool matchesView = IsVirtualShadowPhysicalPageForView(page);
    bool matchesLevel = (page.key1 & 0xFFu) == input.level;
    if ((page.flags & 7u) != 7u || page.key0 != expectedKey0 || !matchesView || !matchesLevel)
    {
        discard;
    }

    uint pagesPerRow = atlasExtent / physicalPageSize;
    uint2 slotOrigin = uint2(physicalIndex % pagesPerRow, physicalIndex / pagesPerRow) * physicalPageSize;
    float reversedDepth = max(1.0f - saturate(input.normalizedDepth), asfloat(1u));
    InterlockedMax(PhysicalAtlas[slotOrigin + pagePixel], asuint(reversedDepth));
}
#endif

#if defined(VE_VSM_STEP_8)
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u0);
RWStructuredBuffer<uint> Statistics : register(u1);

[numthreads(64, 1, 1)]
void VirtualShadowStep8_MarkRenderedCS(uint index : SV_DispatchThreadID)
{
    if (index >= physicalCapacity)
    {
        return;
    }
    PhysicalPage page = PhysicalPages[index];
    bool matchesView = IsVirtualShadowPhysicalPageForView(page);
    if ((page.flags & 6u) == 6u && matchesView)
    {
        page.flags |= 8u;
        page.lastRenderedFrame = frameIndex;
        PhysicalPages[index] = page;
        InterlockedAdd(Statistics[4], 1u);
    }
}
#endif

#if defined(VE_VSM_STEP_9)
RWStructuredBuffer<PhysicalPage> PhysicalPages : register(u0);
RWStructuredBuffer<uint> Statistics : register(u1);

[numthreads(64, 1, 1)]
void VirtualShadowStep9_FinalizeSceneCS(uint index : SV_DispatchThreadID)
{
    if (index >= physicalCapacity)
    {
        return;
    }
    PhysicalPage page = PhysicalPages[index];
    if ((page.flags & 4u) != 0u)
    {
        if ((page.flags & 8u) != 0u)
        {
            page.flags &= ~2u;
        }
        page.flags &= ~4u;
    }
    page.flags &= ~8u;
    PhysicalPages[index] = page;
    if ((page.flags & 1u) != 0u)
    {
        InterlockedAdd(Statistics[0], 1u);
    }
}
#endif

#if defined(VE_VSM_STEP_10)
RWStructuredBuffer<uint> Statistics : register(u0);

[numthreads(1, 1, 1)]
void VirtualShadowStep10_ReuseStatisticsCS(uint3 id : SV_DispatchThreadID)
{
    uint retainedRequested = Statistics[1];
    Statistics[2] = retainedRequested;
    Statistics[3] = 0u;
    Statistics[4] = 0u;
    Statistics[5] = 0u;
}
#endif
