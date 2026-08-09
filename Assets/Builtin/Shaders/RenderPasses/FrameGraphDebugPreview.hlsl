struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    VSOutput output;
    output.uv = float2((vertexID << 1u) & 2u, vertexID & 2u);
    output.position = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return output;
}

#if defined(VE_FRAME_GRAPH_PREVIEW_COLOR)
Texture2D<float4> SourceTexture : register(t0);

uint2 SourceCoordinate(float2 uv)
{
    uint width;
    uint height;
    SourceTexture.GetDimensions(width, height);
    const uint2 dimensions = uint2(width, height);
    return min(uint2(uv * float2(dimensions)), dimensions - 1u);
}

float4 PSColor(VSOutput input) : SV_TARGET
{
    return SourceTexture.Load(int3(SourceCoordinate(input.uv), 0));
}
#endif

#if defined(VE_FRAME_GRAPH_PREVIEW_DEPTH)
Texture2D<float> DepthSourceTexture : register(t0);

uint2 DepthSourceCoordinate(float2 uv)
{
    uint width;
    uint height;
    DepthSourceTexture.GetDimensions(width, height);
    const uint2 dimensions = uint2(width, height);
    return min(uint2(uv * float2(dimensions)), dimensions - 1u);
}

float4 PSDepth(VSOutput input) : SV_TARGET
{
    const float depth = saturate(DepthSourceTexture.Load(int3(DepthSourceCoordinate(input.uv), 0)));
    const float visualDepth = 1.0f - pow(depth, 64.0f);
    return float4(visualDepth, visualDepth, visualDepth, 1.0f);
}
#endif

#if defined(VE_FRAME_GRAPH_PREVIEW_UNSIGNED_INTEGER)
Texture2D<uint> IntegerSourceTexture : register(t0);

uint2 IntegerSourceCoordinate(float2 uv)
{
    uint width;
    uint height;
    IntegerSourceTexture.GetDimensions(width, height);
    const uint2 dimensions = uint2(width, height);
    return min(uint2(uv * float2(dimensions)), dimensions - 1u);
}

uint HashInteger(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return value;
}

float4 PSUnsignedInteger(VSOutput input) : SV_TARGET
{
    const uint value = IntegerSourceTexture.Load(int3(IntegerSourceCoordinate(input.uv), 0));
    if (value == 0u)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const uint hash = HashInteger(value);
    const float3 color = float3(hash & 0xffu, (hash >> 8u) & 0xffu, (hash >> 16u) & 0xffu) / 255.0f;
    return float4(color, 1.0f);
}
#endif
