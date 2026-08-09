struct Clipmap
{
    float4 originAndPageSize;
    float4 radiusAndDepth;
    int4 pageData;
};

struct InvalidationEntry
{
    uint4 data;
};

cbuffer ShadowConstants : register(b4)
{
    float4 lightRight;
    float4 lightUp;
    float4 lightForward;
    float4 atlasAndBias;
    Clipmap clipmaps[24];
    uint atlasExtent;
    uint physicalPageSize;
    uint clipmapCount;
    uint atlasPadding;
    float4x4 inverseViewProjection;
    uint screenWidth;
    uint screenHeight;
    uint physicalCapacity;
    uint frameIndex;
    uint resetCache;
    uint passLevel;
    uint invalidationCount;
    uint padding;
    float4 cameraWorldPosition;
    float4 cameraWorldForward;
    InvalidationEntry invalidatedPages[2048];
    uint viewID;
    uint3 viewIDPadding;
};

struct PhysicalPage
{
    uint key0;
    uint key1;
    uint lastUsedFrame;
    uint lastRenderedFrame;
    uint flags;
    uint3 padding;
};

uint PackVirtualShadowPageKey(int2 pageCoordinate)
{
    return (uint(pageCoordinate.x) & 0xFFFFu) | ((uint(pageCoordinate.y) & 0xFFFFu) << 16u);
}

uint PackVirtualShadowViewLevelKey(uint level)
{
    return level | ((viewID & 0x00FFFFFFu) << 8u);
}

bool IsVirtualShadowPhysicalPageForView(PhysicalPage page)
{
    return ((page.key1 >> 8u) & 0x00FFFFFFu) == (viewID & 0x00FFFFFFu);
}
