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
};

cbuffer ObjectConstants : register(b2, space0)
{
    float4x4 localToWorld;
};

cbuffer MaterialConstants : register(b3, space0)
{
    float4 baseColor;
};

Texture2D MainTexture : register(t0, space0);

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float3 worldNormal : NORMAL0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    float4 worldPosition = mul(localToWorld, float4(input.position, 1.0f));
    output.position = mul(viewProjection, worldPosition);
    output.worldNormal = mul((float3x3)localToWorld, input.normal);
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 normal = normalize(input.worldNormal);
    float3 lightDirection = normalize(directionalLightDirection.xyz);
    float3 lightToSurface = -lightDirection;
    float diffuse = saturate(dot(normal, lightToSurface));
    float3 lightColor = directionalLightColorAndIntensity.rgb;
    float3 litColor = baseColor.rgb * (ambientColor.rgb + (lightColor * directionalLightColorAndIntensity.w * diffuse));
    return float4(saturate(litColor), baseColor.a);
}
