Texture2D sceneColorTexture : register(t5);
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

float4 PSMain(VSOutput input) : SV_TARGET
{
    float2 uv = saturate(input.uv);
    float3 color = sceneColorTexture.Sample(materialSampler, uv).rgb;

    float2 vignetteUv = uv * 2.0f - 1.0f;
    float vignetteRadius = dot(vignetteUv, vignetteUv);
    float vignette = 1.0f - 0.08f * smoothstep(0.28f, 1.1f, vignetteRadius);

    return float4(color * vignette, 1.0f);
}
