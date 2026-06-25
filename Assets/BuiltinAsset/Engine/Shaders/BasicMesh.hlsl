cbuffer ObjectConstants : register(b0, space0)
{
    float4x4 worldViewProjection;
    float4x4 localToWorld;
};

cbuffer MaterialConstants : register(b1, space0)
{
    float4 baseColor;
};

Texture2D MainTexture : register(t0, space0);

cbuffer LightConstants : register(b2, space0)
{
    float4 lightDirectionAndIntensity;
    float4 lightColorAndAmbient;
};

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
    output.position = mul(worldViewProjection, float4(input.position, 1.0f));
    output.worldNormal = mul((float3x3)localToWorld, input.normal);
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 normal = normalize(input.worldNormal);
    float3 lightDirection = normalize(lightDirectionAndIntensity.xyz);
    float3 lightToSurface = -lightDirection;
    float diffuse = saturate(dot(normal, lightToSurface));
    float3 lightColor = lightColorAndAmbient.rgb;
    float ambient = lightColorAndAmbient.a;
    float3 litColor = baseColor.rgb * (ambient + (lightColor * lightDirectionAndIntensity.w * diffuse));
    return float4(saturate(litColor), baseColor.a);
}
