cbuffer ViewConstants : register(b1)
{
    float4x4 viewProjection;
    float4 cameraWorldPosition;
};

Texture2D IconAtlasTexture : register(t0);
SamplerState IconAtlasSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float3 uv : TEXCOORD0;
    float3 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 color : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(viewProjection, float4(input.position, 1.0f));
    output.uv = input.uv.xy;
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float4 atlas = IconAtlasTexture.Sample(IconAtlasSampler, input.uv);
    return float4(saturate(input.color), atlas.a);
}
