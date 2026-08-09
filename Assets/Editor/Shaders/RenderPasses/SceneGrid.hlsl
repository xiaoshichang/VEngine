cbuffer SceneGridFrameConstants : register(b0)
{
    float4 gridParams;
    float4 minorColor;
    float4 majorColor;
    float4 xAxisColor;
    float4 zAxisColor;
};

cbuffer ViewConstants : register(b1)
{
    float4x4 viewProjection;
    float4 cameraWorldPosition;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0f));
    output.worldPosition = input.position;
    return output;
}

float GridLine(float coordinate, float spacing, float width)
{
    float scaledCoordinate = coordinate / spacing;
    float distanceToLine = abs(frac(scaledCoordinate - 0.5f) - 0.5f);
    float antiAlias = max(fwidth(scaledCoordinate), 0.0001f);
    return 1.0f - smoothstep(0.0f, antiAlias * width, distanceToLine);
}

float AxisLine(float coordinate, float width)
{
    float antiAlias = max(fwidth(coordinate), 0.0001f);
    return 1.0f - smoothstep(0.0f, antiAlias * width, abs(coordinate));
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float unitSize = max(gridParams.x, 0.001f);
    float opacity = saturate(gridParams.y);
    float lineWidth = max(gridParams.z, 0.5f);
    float majorEvery = max(gridParams.w, 1.0f);
    float distanceFade = 1.0f - saturate(length(input.worldPosition.xz) / 600.0f);

    float minorLine = max(GridLine(input.worldPosition.x, unitSize, lineWidth), GridLine(input.worldPosition.z, unitSize, lineWidth));
    float majorSpacing = unitSize * majorEvery;
    float majorLine = max(GridLine(input.worldPosition.x, majorSpacing, lineWidth * 1.35f), GridLine(input.worldPosition.z, majorSpacing, lineWidth * 1.35f));
    float xAxis = AxisLine(input.worldPosition.z, lineWidth * 1.8f);
    float zAxis = AxisLine(input.worldPosition.x, lineWidth * 1.8f);

    float4 color = minorColor;
    color = lerp(color, majorColor, saturate(majorLine));
    color = lerp(color, xAxisColor, saturate(xAxis));
    color = lerp(color, zAxisColor, saturate(zAxis));
    color.a *= saturate(max(max(minorLine, majorLine), max(xAxis, zAxis)) * opacity * (0.25f + distanceFade * 0.75f));
    return color;
}
