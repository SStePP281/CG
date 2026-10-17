#include "PPSUtil.hlsl"

Texture2D gSpotTex : register(t1);
Texture2D gNormal : register(t2);
Texture2D gDepth : register(t3);

cbuffer cbWaterSpot : register(b0)
{
    float4x4 gInvViewProj;
    float gNoiseScale;
    float3 pad;
};

float3 ReconstructPosition(float2 texCoord, float depth)
{
    float x = texCoord.x * 2.0f - 1.0f;
    float y = (1.0f - texCoord.y) * 2.0f - 1.0f;

    float4 ndcPos = float4(x, y, depth, 1.0f);
    float4 worldPos = mul(ndcPos, gInvViewProj);

    return worldPos.xyz / worldPos.w;
}

float4 PS(VSOut pin) : SV_Target
{
    float2 uv = pin.TexC;

    float3 base = gLastStep.Sample(gsamPointClamp, uv).rgb;
    float3 worldNormal = gNormal.Sample(gsamPointClamp, uv).rgb;
    float depth = gDepth.Sample(gsamPointClamp, uv).r;
    float3 worldPos = ReconstructPosition(uv, depth);

    float4 spot = WorldAlignedTexture(gSpotTex, worldPos, worldNormal, float3(gNoiseScale, gNoiseScale, gNoiseScale), 4.0f);

    return float4(base * spot.rgb, 1.0f);
}