struct VertexInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

cbuffer CameraConstants : register(b0, space0)
{
    row_major float4x4 viewProjection;
};

Texture2D BaseColorTexture : register(t0, space1);
SamplerState BaseColorSampler : register(s0, space1);

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    output.uv = input.uv;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    return BaseColorTexture.Sample(BaseColorSampler, input.uv);
}
