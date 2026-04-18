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
SamplerState materialSampler : register(s0);

static const float ALPHA_MODE_MASK = 1.0f;

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
    float2 texCoord : TEXCOORD0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    float4 worldPosition = mul(world, float4(input.position, 1.0f));
    output.position = mul(lightViewProjection, worldPosition);
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

void PSMain(PSInput input)
{
    bool isAlphaMask = textureControls.z > ALPHA_MODE_MASK - 0.5f && textureControls.z < ALPHA_MODE_MASK + 0.5f;
    if (isAlphaMask)
    {
        float2 uv = input.texCoord * roughnessUvScaleAlphaCutoff.yz;
        float alpha = input.color.a * baseColorFactor.a * baseColorTexture.Sample(materialSampler, uv).a;
        clip(alpha - roughnessUvScaleAlphaCutoff.w);
    }
}
