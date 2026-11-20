#ifndef SHADOW_HLSLI
#define SHADOW_HLSLI

#ifndef CASCADES_COUNT
#define CASCADES_COUNT 3
#endif

static const float3 CASCADES_COLORS[CASCADES_COUNT] =
{
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f }
};

struct CascadeData
{
    float4x4 gLightViewProj;
    float4x4 gShadowTransform;
    float4 gDistances;
    float4 padding[7];
};

cbuffer cbShadow : register(b2)
{
    CascadeData gCascades[CASCADES_COUNT];
};

Texture2DArray gShadowMap : register(t5);
SamplerComparisonState gsamShadow : register(s6);

uint GetCascadesLayer(float viewZ)
{
    uint cascade = 0;

    [unroll]
    for (uint j = 0; j < CASCADES_COUNT - 1; j++)
    {
        if (viewZ > gCascades[0].gDistances[j])
        {
            cascade = j + 1;
        }
    }

    return min(cascade, (uint) (CASCADES_COUNT - 1));
}

float CalcShadow(float3 posW, float viewZ)
{
    uint cascade = GetCascadesLayer(viewZ);

    float4 shadowPos = mul(float4(posW, 1.0f), gCascades[cascade].gShadowTransform);
    shadowPos.xyz /= shadowPos.w;

    if (shadowPos.x < 0.0f || shadowPos.x > 1.0f || shadowPos.y < 0.0f || shadowPos.y > 1.0f)
    {
        return 1.0f;
    }

    float width, height, elements;
    gShadowMap.GetDimensions(width, height, elements);
    float2 dx = 1.0f / float2(width, height);

    float shadow = 0.0f;
    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            float2 offset = float2(x, y) * dx;
            shadow += gShadowMap.SampleCmpLevelZero(
                gsamShadow,
                float3(shadowPos.xy + offset, (float) cascade),
                shadowPos.z - 0.001f
            );
        }
    }
    return shadow / 9.0f;
}

#endif // SHADOW_HLSLI
