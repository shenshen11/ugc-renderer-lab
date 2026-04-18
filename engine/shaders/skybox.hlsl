cbuffer SceneConstants : register(b0)
{
    float4 cameraPositionAndEnvironmentIntensity;
    float4 directionalLightDirectionAndIntensity;
    float4 directionalLightColorAndExposure;
    float4 cameraRightAndTanHalfFovX;
    float4 cameraUpAndTanHalfFovY;
    float4 cameraForward;
}

Texture2D environmentTexture : register(t5);
SamplerState materialSampler : register(s0);

static const float INV_PI = 0.31830988618f;
static const float INV_TWO_PI = 0.15915494309f;

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 ndc : TEXCOORD0;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    float2 positions[3] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f, 3.0f),
        float2(3.0f, -1.0f),
    };

    VSOutput output;
    output.position = float4(positions[vertexId], 0.0f, 1.0f);
    output.ndc = positions[vertexId];
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

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 viewDirection = normalize(
        cameraForward.xyz +
        input.ndc.x * cameraRightAndTanHalfFovX.xyz * cameraRightAndTanHalfFovX.w +
        input.ndc.y * cameraUpAndTanHalfFovY.xyz * cameraUpAndTanHalfFovY.w);

    float2 uv = DirectionToLatLongUv(viewDirection);
    float environmentIntensity = cameraPositionAndEnvironmentIntensity.w;
    float exposure = directionalLightColorAndExposure.w;
    float3 color = SRGBToLinear(environmentTexture.Sample(materialSampler, uv).rgb) * environmentIntensity;
    float3 mapped = ACESFilm(color * exposure);
    return float4(LinearToSRGB(mapped), 1.0f);
}
