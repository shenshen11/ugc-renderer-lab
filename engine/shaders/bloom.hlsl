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

Texture2D sourceTexture : register(t5);
SamplerState materialSampler : register(s0);

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

float Luminance(float3 value)
{
    return dot(value, float3(0.2126f, 0.7152f, 0.0722f));
}

float4 PSPrefilter(VSOutput input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    float2 texelSize = frameBufferParams.xy;
    float3 color =
        sourceTexture.Sample(materialSampler, saturate(uv + texelSize * float2(-1.0f, -1.0f))).rgb * 0.125f +
        sourceTexture.Sample(materialSampler, saturate(uv + texelSize * float2(1.0f, -1.0f))).rgb * 0.125f +
        sourceTexture.Sample(materialSampler, saturate(uv + texelSize * float2(-1.0f, 1.0f))).rgb * 0.125f +
        sourceTexture.Sample(materialSampler, saturate(uv + texelSize * float2(1.0f, 1.0f))).rgb * 0.125f +
        sourceTexture.Sample(materialSampler, uv).rgb * 0.5f;

    float threshold = bloomParams.x;
    float softKnee = max(bloomParams.y * threshold, 1e-4f);
    float brightness = Luminance(color);
    float soft = saturate((brightness - threshold + softKnee) / (2.0f * softKnee));
    float contribution = max(brightness - threshold, 0.0f) + soft * soft * softKnee;
    contribution /= max(brightness, 1e-4f);

    return float4(color * contribution, 1.0f);
}

float3 BlurSample(float2 uv, float2 texelSize)
{
    return
        sourceTexture.Sample(materialSampler, saturate(uv - texelSize * 2.0f)).rgb * 0.12162162f +
        sourceTexture.Sample(materialSampler, saturate(uv - texelSize)).rgb * 0.23324324f +
        sourceTexture.Sample(materialSampler, uv).rgb * 0.29027027f +
        sourceTexture.Sample(materialSampler, saturate(uv + texelSize)).rgb * 0.23324324f +
        sourceTexture.Sample(materialSampler, saturate(uv + texelSize * 2.0f)).rgb * 0.12162162f;
}

float4 PSBlurHorizontal(VSOutput input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    float2 texelSize = float2(frameBufferParams.z, 0.0f) * bloomParams.w;
    return float4(BlurSample(uv, texelSize), 1.0f);
}

float4 PSBlurVertical(VSOutput input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    float2 texelSize = float2(0.0f, frameBufferParams.w) * bloomParams.w;
    return float4(BlurSample(uv, texelSize), 1.0f);
}
