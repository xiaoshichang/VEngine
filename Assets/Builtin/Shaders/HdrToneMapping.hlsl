cbuffer HdrConstants : register(b0, space0)
{
    float4 hdrSettings;
};

Texture2D HdrSceneColor : register(t0, space0);
SamplerState LinearClampSampler : register(s0, space0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;
    const float2 positions[3] = {float2(-1.0f, -1.0f), float2(-1.0f, 3.0f), float2(3.0f, -1.0f)};
    const float2 uvs[3] = {float2(0.0f, 1.0f), float2(0.0f, -1.0f), float2(2.0f, 1.0f)};
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.uv = uvs[vertexId];
    return output;
}

float3 Aces(float3 color)
{
    const float3 numerator = color * (2.51f * color + 0.03f);
    const float3 denominator = color * (2.43f * color + 0.59f) + 0.14f;
    return saturate(numerator / max(denominator, 0.0001f));
}

float LinearToSrgb(float value)
{
    return value <= 0.0031308f ? value * 12.92f : 1.055f * pow(max(value, 0.0f), 1.0f / 2.4f) - 0.055f;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 color = HdrSceneColor.Sample(LinearClampSampler, input.uv).rgb * exp2(hdrSettings.x);
    color = hdrSettings.y < 0.5f ? color / (1.0f + color) : Aces(color / max(hdrSettings.z, 0.001f));
    color = float3(LinearToSrgb(color.r), LinearToSrgb(color.g), LinearToSrgb(color.b));
    return float4(saturate(color), 1.0f);
}
