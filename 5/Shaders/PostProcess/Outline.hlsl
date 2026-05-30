#include "PPSUtil.hlsl"

Texture2D gDepth : register(t1);
Texture2D gNormal : register(t2);

cbuffer cbOutline : register(b0)
{
    float2 gTexelSize;
    float gKernelSize;
    float pad0;
    float2 gDepthThreshold;
    float2 gNormalThreshold;
    float3 gOutlineColor;
    float pad1;
};

float4 LaplasFilter(float2 uv)
{
    float KERNEL_SIZE = floor(gKernelSize);
    if (KERNEL_SIZE < 2.0f)
    {
        return float4(0, 0, 0, 0);
    }
    float HALF_KERNEL_SIZE = floor(KERNEL_SIZE / 2.0f);
    float HALF_KERNEL_SIZE_SQ = KERNEL_SIZE * KERNEL_SIZE / 4.0f;
    float3 laplacianNormal = float3(0, 0, 0);
    float laplacianDepth = 0.0f;
    float centerWeight = 0.0f;
    for (float y = -HALF_KERNEL_SIZE; y <= HALF_KERNEL_SIZE; y++)
    {
        for (float x = -HALF_KERNEL_SIZE; x <= HALF_KERNEL_SIZE; x++)
        {
            float2 coord = float2(x, y);
            if (dot(coord, coord) > HALF_KERNEL_SIZE_SQ)
            {
                continue;
            }
            centerWeight++;
            float2 pixelUV = uv + gTexelSize * coord;
            laplacianNormal -= gNormal.Sample(gsamPointClamp, pixelUV).rgb;
            laplacianDepth -= gDepth.Sample(gsamPointClamp, pixelUV).r;
        }
    }
    laplacianNormal += gNormal.Sample(gsamPointClamp, uv).rgb * centerWeight;
    laplacianDepth += gDepth.Sample(gsamPointClamp, uv).r * centerWeight;
    centerWeight--;
    centerWeight = 1.0f / centerWeight;
    laplacianNormal *= centerWeight;
    laplacianDepth *= centerWeight;
    return float4(laplacianNormal, laplacianDepth);
}

float4 PS(VSOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    float4 laplas = LaplasFilter(uv);
    float depthST = InvLerpClamped(gDepthThreshold.r, gDepthThreshold.g, laplas.a);
    float normalST = InvLerpClamped(gNormalThreshold.r, gNormalThreshold.g, CubNormalize(laplas.rgb));
    float edge = saturate(max(depthST, normalST));
    float3 color = gLastStep.Sample(gsamPointClamp, uv).rgb;
    return float4(lerp(color, gOutlineColor, edge), 1.0f);
}