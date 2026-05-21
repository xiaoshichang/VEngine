struct VertexInput
{
    float3 position : POSITION;
};

struct VertexOutput
{
    float4 position : SV_Position;
};

cbuffer CameraConstants
{
    row_major float4x4 viewProjection;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    return float4(input.position.xyz, 1.0);
}
