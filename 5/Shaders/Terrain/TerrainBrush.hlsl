cbuffer cbBrush : register(b0)
{
    int gLayer;
    int gMode;
    float gRadius;
    float gStrengthDt;

    int gMaskCount;
    int gLayerCount;
    float gWorldSize;
    int gMaskSize;
};

RWTexture2DArray<unorm float4> gMask : register(u0);

struct Hit
{
    float3 PosW;
    uint Valid;
    float2 MapUV;
    float2 Pad;
};
RWStructuredBuffer<Hit> gHit : register(u1);

[numthreads(8, 8, 1)]
void CS(uint3 id : SV_DispatchThreadID)
{
    Hit hit = gHit[0];
    if (hit.Valid == 0)
    {
        return;
    }

    float texelMeters = gWorldSize / gMaskSize;
    int radiusTexels = (int)ceil(gRadius / texelMeters);

    float2 centerTexel = hit.MapUV * gMaskSize;
    int2 pixel = (int2) centerTexel - radiusTexels + (int2)id.xy;

    if (any(pixel < 0) || any(pixel >= gMaskSize))
    {
        return;
    }

    float dist = length((pixel + 0.5f - centerTexel) * texelMeters);
    if (dist > gRadius)
    {
        return;
    }

    float amount = gStrengthDt * (1.0f - dist / gRadius);

    float w[16];
    for (int i = 0; i < 16; i++)
    {
        w[i] = 0.0f;
    }

    for (int m = 0; m < gMaskCount; m++)
    {
        float4 v = gMask[int3(pixel, m)];
        w[m * 4 + 0] = v.x;
        w[m * 4 + 1] = v.y;
        w[m * 4 + 2] = v.z;
        w[m * 4 + 3] = v.w;
    }

    if (gMode == 0)
    {
        w[gLayer] += amount;
    }
    else
    {
        float take = min(w[gLayer], amount);
        w[gLayer] -= take;
        w[0] += take;
    }

    float sum = 0.0f;
    for (int j = 0; j < gLayerCount; j++)
    {
        sum += w[j];
    }
    sum = max(sum, 0.0001f);

    for (int k = 0; k < gMaskCount; k++)
    {
        float4 v = float4(w[k * 4 + 0], w[k * 4 + 1], w[k * 4 + 2], w[k * 4 + 3]) / sum;
        gMask[int3(pixel, k)] = v;
    }
}