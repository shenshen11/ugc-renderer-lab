cbuffer SceneConstants : register(b0)
{
    float4 cameraPositionAndEnvironmentIntensity;
    float4 directionalLightDirectionAndIntensity;
    float4 directionalLightColorAndExposure;
    float4 cameraRightAndTanHalfFovX;
    float4 cameraUpAndTanHalfFovY;
    float4 cameraForward;
    float4x4 lightViewProjection;
    float4 shadowParams;
    float4 bloomParams;
    float4 iblParams;
    float4 frameBufferParams;
    float4 postProcessParams;
}

Texture2D sceneColorTexture : register(t5);
Texture2D bloomTexture : register(t6);
Texture2D shadowMapTexture : register(t8);
SamplerState materialSampler : register(s0);
SamplerState shadowSampler : register(s1);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f),
    };

    float2 uvs[3] = {
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(2.0f, 1.0f),
    };

    VSOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.uv = uvs[vertexId];
    return output;
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

float3 LinearToSRGB(float3 value)
{
    return pow(saturate(value), 1.0f / 2.2f);
}

float3 Heatmap(float value)
{
    float clampedValue = saturate(value);
    float3 cold = float3(0.0f, 0.1f, 0.8f);
    float3 mid = float3(0.05f, 0.85f, 0.25f);
    float3 hot = float3(1.0f, 0.25f, 0.02f);
    float3 lowRange = lerp(cold, mid, saturate(clampedValue * 2.0f));
    float3 highRange = lerp(mid, hot, saturate((clampedValue - 0.5f) * 2.0f));
    return clampedValue < 0.5f ? lowRange : highRange;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    float exposure = directionalLightColorAndExposure.w;
    float3 hdrColor = sceneColorTexture.Sample(materialSampler, uv).rgb;
    float3 bloomColor = bloomTexture.Sample(materialSampler, uv).rgb * bloomParams.z;
    float3 compositeColor = (hdrColor + bloomColor) * exposure;
    int debugMode = (int)round(postProcessParams.x);

    float2 vignetteUv = uv * 2.0f - 1.0f;
    float vignetteRadius = dot(vignetteUv, vignetteUv);
    float vignette = 1.0f - 0.08f * smoothstep(0.28f, 1.1f, vignetteRadius);

    if (debugMode == 1)
    {
        return float4(LinearToSRGB(ACESFilm(hdrColor * exposure)), 1.0f);
    }

    if (debugMode == 2)
    {
        return float4(LinearToSRGB(ACESFilm(bloomColor * exposure * 4.0f)), 1.0f);
    }

    if (debugMode == 3)
    {
        float luminance = dot(hdrColor, float3(0.2126f, 0.7152f, 0.0722f)) * 0.18f;
        return float4(Heatmap(luminance), 1.0f);
    }

    if (debugMode == 4)
    {
        float shadowDepth = shadowMapTexture.SampleLevel(shadowSampler, uv, 0).r;
        return float4(Heatmap(shadowDepth), 1.0f);
    }

    if (debugMode >= 5 && debugMode <= 7)
    {
        return float4(saturate(hdrColor), 1.0f);
    }

    float3 mappedColor = ACESFilm(compositeColor) * vignette;

    return float4(LinearToSRGB(mappedColor), 1.0f);
}
