#include "PPSUtil.hlsl"

Texture2D gColor : register(t1);

float4 PS(VSOut pin) : SV_Target
{
    float2 uv = pin.TexC;
    
    float3 input0 = gLastStep.Sample(gsamPointClamp, uv).rgb;
    float3 color = max(gColor.Sample(gsamPointClamp, uv).rgb, 0.00001f);
    
    float3 tone = RGBtoHSV(input0 / color);
    float v = pow(2.0f, round(log2(tone.b)));
    float3 steps = HSVtoRGB(float3(tone.rg, v));
    float3 result = steps * color;
    
    return float4(result, 1.0f);
}