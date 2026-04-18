cbuffer SceneConstants : register(b0)
{
    float4 cameraPositionAndEnvironmentIntensity;
    float4 directionalLightDirectionAndIntensity;
    float4 directionalLightColorAndExposure;
    float4 cameraRightAndTanHalfFovX;
    float4 cameraUpAndTanHalfFovY;
    float4 cameraForward;
}

cbuffer ObjectConstants : register(b1)
{
    float4x4 world;
    float4x4 worldViewProjection;
    float4x4 worldInverseTranspose;
}

cbuffer MaterialConstants : register(b2)
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
Texture2D environmentTexture : register(t5);
SamplerState materialSampler : register(s0);

static const float PI = 3.14159265359f;
static const float INV_PI = 0.31830988618f;
static const float INV_TWO_PI = 0.15915494309f;
static const float ALPHA_MODE_MASK = 1.0f;
static const float ALPHA_MODE_BLEND = 2.0f;

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

float2 DirectionToLatLongUv(float3 direction)
{
    direction = normalize(direction);
    float longitude = atan2(direction.z, direction.x);
    float latitude = acos(clamp(direction.y, -1.0f, 1.0f));
    return float2(0.5f + longitude * INV_TWO_PI, latitude * INV_PI);
}

float3 SampleEnvironment(float3 direction)
{
    float2 uv = DirectionToLatLongUv(direction);
    return SRGBToLinear(environmentTexture.Sample(materialSampler, uv).rgb);
}

float2 EnvBRDFApprox(float roughness, float ndotv)
{
    float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28f * ndotv)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

void BuildBasis(float3 normal, out float3 tangent, out float3 bitangent)
{
    float3 up = abs(normal.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    tangent = normalize(cross(up, normal));
    bitangent = normalize(cross(normal, tangent));
}

float3 SampleSpecularEnvironment(float3 reflectionDirection, float3 surfaceNormal, float roughness)
{
    float3 tangent;
    float3 bitangent;
    BuildBasis(reflectionDirection, tangent, bitangent);

    float sampleSpread = roughness * roughness * 0.65f;
    float normalBias = roughness * 0.28f;

    float3 sampleDirection0 = normalize(reflectionDirection);
    float3 sampleDirection1 = normalize(reflectionDirection + tangent * sampleSpread + surfaceNormal * normalBias);
    float3 sampleDirection2 = normalize(reflectionDirection - tangent * sampleSpread + surfaceNormal * normalBias);
    float3 sampleDirection3 = normalize(reflectionDirection + bitangent * sampleSpread + surfaceNormal * normalBias);
    float3 sampleDirection4 = normalize(reflectionDirection - bitangent * sampleSpread + surfaceNormal * normalBias);

    float3 accumulated =
        SampleEnvironment(sampleDirection0) * 0.36f +
        SampleEnvironment(sampleDirection1) * 0.16f +
        SampleEnvironment(sampleDirection2) * 0.16f +
        SampleEnvironment(sampleDirection3) * 0.16f +
        SampleEnvironment(sampleDirection4) * 0.16f;

    return accumulated;
}

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_TARGET
{
    float2 uv = input.texCoord * roughnessUvScaleAlphaCutoff.yz;
    float4 baseSample = baseColorTexture.Sample(materialSampler, uv);
    float3 tangentSpaceNormal = normalTexture.Sample(materialSampler, uv).xyz * 2.0f - 1.0f;
    float4 metallicRoughnessSample = metallicRoughnessTexture.Sample(materialSampler, uv);
    float4 occlusionSample = occlusionTexture.Sample(materialSampler, uv);
    float3 emissiveSample = emissiveTexture.Sample(materialSampler, uv).rgb;
    float4 baseColor = input.color * baseColorFactor * baseSample;
    bool isAlphaMask = textureControls.z > ALPHA_MODE_MASK - 0.5f && textureControls.z < ALPHA_MODE_MASK + 0.5f;
    bool isAlphaBlend = textureControls.z > ALPHA_MODE_BLEND - 0.5f;
    bool isDoubleSided = textureControls.w > 0.5f;

    if (isAlphaMask)
    {
        clip(baseColor.a - roughnessUvScaleAlphaCutoff.w);
    }

    tangentSpaceNormal.xy *= textureControls.xx;
    tangentSpaceNormal.z = sqrt(saturate(1.0f - dot(tangentSpaceNormal.xy, tangentSpaceNormal.xy)));

    float3 worldNormal = normalize(input.worldNormal);
    float3 worldTangent = normalize(input.worldTangent - dot(input.worldTangent, worldNormal) * worldNormal);
    float3 worldBitangent = normalize(cross(worldNormal, worldTangent)) * input.tangentSign;
    float3 normal = normalize(
        worldTangent * tangentSpaceNormal.x +
        worldBitangent * tangentSpaceNormal.y +
        worldNormal * tangentSpaceNormal.z);
    if (isDoubleSided && !isFrontFace)
    {
        normal = -normal;
    }

    float3 lightDirection = normalize(directionalLightDirectionAndIntensity.xyz);
    float3 lightColor = directionalLightColorAndExposure.rgb;
    float lightIntensity = directionalLightDirectionAndIntensity.w;
    float environmentIntensity = cameraPositionAndEnvironmentIntensity.w;
    float exposure = directionalLightColorAndExposure.w;
    float3 viewDirection = normalize(cameraPositionAndEnvironmentIntensity.xyz - input.worldPosition);
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
    float3 radiance = lightColor * lightIntensity;
    float3 directLighting = (kd * albedo / PI + specular) * radiance * ndotl;

    float3 diffuseEnvironment = SampleEnvironment(normal);
    float3 reflectionDirection = reflect(-viewDirection, normal);
    float3 roughReflectionDirection = normalize(lerp(reflectionDirection, normal, roughness * roughness * 0.45f));
    float3 specularEnvironment = SampleSpecularEnvironment(roughReflectionDirection, normal, roughness);
    float2 envBrdf = EnvBRDFApprox(roughness, ndotv);
    float3 ambientDiffuse = diffuseEnvironment * kd * albedo * occlusion * environmentIntensity;
    float3 ambientSpecular =
        specularEnvironment *
        (f0 * envBrdf.x + envBrdf.y) *
        occlusion *
        environmentIntensity;
    float3 color = ambientDiffuse + ambientSpecular + directLighting + emissive;
    float3 mapped = ACESFilm(color * exposure);
    float outputAlpha = isAlphaBlend ? baseColor.a : 1.0f;

    return float4(LinearToSRGB(mapped), outputAlpha);
}
