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
    float4 textureControls;
}

Texture2D baseColorTexture : register(t0);
Texture2D normalTexture : register(t1);
Texture2D metallicRoughnessTexture : register(t2);
Texture2D occlusionTexture : register(t3);
Texture2D emissiveTexture : register(t4);
SamplerState materialSampler : register(s0);

struct VSInput
{
    float3 position : POSITION;
    float4 color : COLOR;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float3 worldPosition : TEXCOORD1;
    float3 worldNormal : TEXCOORD2;
    float3 worldTangent : TEXCOORD3;
    float tangentSign : TEXCOORD4;
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
    float3 worldTangent = mul((float3x3)world, input.tangent.xyz);
    output.worldTangent = normalize(worldTangent - dot(worldTangent, output.worldNormal) * output.worldNormal);
    output.tangentSign = input.tangent.w;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = input.texCoord * roughnessUvScaleAlphaCutoff.yz;
    float4 baseSample = baseColorTexture.Sample(materialSampler, uv);
    float3 tangentSpaceNormal = normalTexture.Sample(materialSampler, uv).xyz * 2.0f - 1.0f;
    float4 metallicRoughnessSample = metallicRoughnessTexture.Sample(materialSampler, uv);
    float4 occlusionSample = occlusionTexture.Sample(materialSampler, uv);
    float3 emissiveSample = emissiveTexture.Sample(materialSampler, uv).rgb;
    float4 baseColor = input.color * baseColorFactor * baseSample;

    tangentSpaceNormal.xy *= textureControls.xx;
    tangentSpaceNormal.z = sqrt(saturate(1.0f - dot(tangentSpaceNormal.xy, tangentSpaceNormal.xy)));

    float3 worldNormal = normalize(input.worldNormal);
    float3 worldTangent = normalize(input.worldTangent - dot(input.worldTangent, worldNormal) * worldNormal);
    float3 worldBitangent = normalize(cross(worldNormal, worldTangent)) * input.tangentSign;
    float3 normal = normalize(
        worldTangent * tangentSpaceNormal.x +
        worldBitangent * tangentSpaceNormal.y +
        worldNormal * tangentSpaceNormal.z);
    float3 lightDirection = normalize(float3(0.35f, 0.8f, -0.45f));
    float3 viewDirection = normalize(cameraPosition - input.worldPosition);
    float3 halfVector = normalize(lightDirection + viewDirection);

    float metallic = saturate(emissiveFactorAndMetallic.w * metallicRoughnessSample.b);
    float roughness = saturate(roughnessUvScaleAlphaCutoff.x * metallicRoughnessSample.g);
    float occlusion = lerp(1.0f, occlusionSample.r, saturate(textureControls.y));
    float ndotl = saturate(dot(normal, lightDirection));
    float ndoth = saturate(dot(normal, halfVector));
    float specularPower = lerp(96.0f, 8.0f, roughness);

    float3 albedo = baseColor.rgb;
    float3 diffuseColor = albedo * (1.0f - metallic);
    float3 specularColor = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 ambient = albedo * 0.18f * occlusion;
    float3 directDiffuse = diffuseColor * ndotl;
    float3 directSpecular = specularColor * pow(ndoth, specularPower) * ndotl;
    float3 emissive = emissiveFactorAndMetallic.rgb * emissiveSample;

    return float4(ambient + directDiffuse + directSpecular + emissive, baseColor.a);
}
