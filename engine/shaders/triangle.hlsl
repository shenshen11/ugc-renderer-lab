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

static const float PI = 3.14159265359f;

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

float3 SRGBToLinear(float3 value)
{
    return pow(saturate(value), 2.2f);
}

float3 LinearToSRGB(float3 value)
{
    return pow(saturate(value), 1.0f / 2.2f);
}

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float ndoth = saturate(dot(normal, halfVector));
    float ndothSquared = ndoth * ndoth;
    float denominator = ndothSquared * (alphaSquared - 1.0f) + 1.0f;
    return alphaSquared / max(PI * denominator * denominator, 1e-4f);
}

float GeometrySchlickGGX(float cosineAngle, float roughness)
{
    float remapped = roughness + 1.0f;
    float k = (remapped * remapped) / 8.0f;
    return cosineAngle / lerp(cosineAngle, 1.0f, k);
}

float GeometrySmith(float3 normal, float3 viewDirection, float3 lightDirection, float roughness)
{
    float ndotv = saturate(dot(normal, viewDirection));
    float ndotl = saturate(dot(normal, lightDirection));
    return GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
}

float3 FresnelSchlick(float cosineTheta, float3 f0)
{
    return f0 + (1.0f - f0) * pow(1.0f - cosineTheta, 5.0f);
}

float3 FresnelSchlickRoughness(float cosineTheta, float3 f0, float roughness)
{
    return f0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), f0) - f0) * pow(1.0f - cosineTheta, 5.0f);
}

float3 ACESFilm(float3 value)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((value * (a * value + b)) / (value * (c * value + d) + e));
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
    float roughness = clamp(roughnessUvScaleAlphaCutoff.x * metallicRoughnessSample.g, 0.045f, 1.0f);
    float occlusion = saturate(1.0f + textureControls.y * (occlusionSample.r - 1.0f));
    float ndotl = saturate(dot(normal, lightDirection));
    float ndotv = saturate(dot(normal, viewDirection));
    float hdotv = saturate(dot(halfVector, viewDirection));

    float3 albedo = SRGBToLinear(baseColor.rgb);
    float3 emissive = SRGBToLinear(emissiveSample) * emissiveFactorAndMetallic.rgb;
    float3 f0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float3 fresnel = FresnelSchlick(hdotv, f0);
    float distribution = DistributionGGX(normal, halfVector, roughness);
    float geometry = GeometrySmith(normal, viewDirection, lightDirection, roughness);
    float3 numerator = distribution * geometry * fresnel;
    float denominator = max(4.0f * ndotv * ndotl, 1e-4f);
    float3 specular = numerator / denominator;

    float3 ks = fresnel;
    float3 kd = (1.0f - ks) * (1.0f - metallic);
    float3 radiance = float3(5.0f, 4.75f, 4.5f);
    float3 directLighting = (kd * albedo / PI + specular) * radiance * ndotl;

    float3 ambientDiffuse = 0.04f * kd * albedo * occlusion;
    float3 ambientSpecular = 0.02f * FresnelSchlickRoughness(ndotv, f0, roughness) * occlusion;
    float3 color = ambientDiffuse + ambientSpecular + directLighting + emissive;
    float3 mapped = ACESFilm(color);

    return float4(LinearToSRGB(mapped), baseColor.a);
}
