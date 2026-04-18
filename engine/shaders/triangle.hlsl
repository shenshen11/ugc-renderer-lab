cbuffer ObjectConstants : register(b0)
{
    float4x4 mvp;
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
    float2 texCoord : TEXCOORD;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float4 color : COLOR;
    float2 texCoord : TEXCOORD;
};

PSInput VSMain(VSInput input)
{
    PSInput output;
    output.position = mul(mvp, float4(input.position, 1.0f));
    output.color = input.color;
    output.texCoord = input.texCoord;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    float2 uv = input.texCoord * roughnessUvScaleAlphaCutoff.yz;
    return input.color * baseColorFactor * materialTexture.Sample(materialSampler, uv);
}
