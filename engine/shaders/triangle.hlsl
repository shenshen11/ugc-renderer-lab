cbuffer ObjectConstants : register(b0)
{
    float4x4 world;
    float4x4 worldViewProjection;
    float4x4 worldInverseTranspose;
    float3 cameraPosition;
    float objectPadding;
}

cbuffer MaterialConstants : register(b1)
{
    float4 baseColorFactor;
    float4 emissiveFactorAndMetallic;
    float4 roughnessUvScaleAlphaCutoff;
}

Texture2D materialTexture : register(t0);
SamplerState materialSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float3 worldPosition : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float2 texCoord : TEXCOORD;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    float4 worldPosition = mul(world, float4(input.position, 1.0f));
    output.position = mul(worldViewProjection, float4(input.position, 1.0f));
    output.color = input.color;
    output.texCoord = input.texCoord;
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = normalize(mul((float3x3)worldInverseTranspose, input.normal));
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = input.texCoord * roughnessUvScaleAlphaCutoff.yz;
    float4 baseSample = materialTexture.Sample(materialSampler, uv);
    float4 baseColor = input.color * baseColorFactor * baseSample;

    float3 normal = normalize(input.worldNormal);
    float3 lightDirection = normalize(float3(0.35f, 0.8f, -0.45f));
    float3 viewDirection = normalize(cameraPosition - input.worldPosition);
    float3 halfVector = normalize(lightDirection + viewDirection);

    float metallic = saturate(emissiveFactorAndMetallic.w);
    float roughness = saturate(roughnessUvScaleAlphaCutoff.x);
    float ndotl = saturate(dot(normal, lightDirection));
    float ndoth = saturate(dot(normal, halfVector));
    float specularPower = lerp(96.0f, 8.0f, roughness);

    float3 albedo = baseColor.rgb;
    float3 diffuseColor = albedo * (1.0f - metallic);
    float3 specularColor = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 ambient = albedo * 0.18f;
    float3 directDiffuse = diffuseColor * ndotl;
    float3 directSpecular = specularColor * pow(ndoth, specularPower) * ndotl;
    float3 emissive = emissiveFactorAndMetallic.rgb;

    return float4(ambient + directDiffuse + directSpecular + emissive, baseColor.a);
}
