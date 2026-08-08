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

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_POSITION;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = mul(viewProjection, mul(localToWorld, float4(input.position, 1.0f)));
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return baseColor;
}
