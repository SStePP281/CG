#include "PPSUtil.hlsl"

static const float EXPOSURE = 0.8f;

float3 ToneMapReinhard(float3 hdr, float exposure)
{
    hdr *= exposure;
    return hdr / (hdr + 1.0f);
}

float3 ToneMapACES(float3 x)
{
    float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PS(VSOut pin) : SV_Target
{
    float3 hdr = gLastStep.Sample(gsamPointClamp, pin.TexC).rgb;
    float3 ldr = ToneMapReinhard(hdr, EXPOSURE);
    return float4(ldr, 1.0f);
}