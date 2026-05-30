#ifndef PPS_UTIL_HLSL
#define PPS_UTIL_HLSL

Texture2D gLastStep : register(t0);

SamplerState gsamPointClamp : register(s1);

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

VSOut VS(uint vid : SV_VertexID)
{
    VSOut vout;
    vout.TexC = float2((vid << 1) & 2, vid & 2);
    vout.PosH = float4(vout.TexC * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return vout;
}

float InvLerp(float a, float b, float v)
{
    return (v - a) / (b - a);
}

float InvLerpClamped(float a, float b, float v)
{
    return saturate(InvLerp(a, b, v));
}

float CubNormalize(float3 v)
{
    return max(v.r, max(v.g, v.b));
}

float CheapContrast(float x, float contrast)
{
    return saturate(x * (1.0f + contrast) - contrast * 0.5f);
}

float3 RGBtoHSV(float3 rgb)
{
    float r = rgb.r, g = rgb.g, b = rgb.b;
    
    float cmax = max(r, max(g, b));
    float cmin = min(r, min(g, b));
    float delta = cmax - cmin;
    
    float h = 0.0f;
    if (delta > 0.0f)
    {
        if (cmax == r)
            h = 60.0f * fmod((g - b) / delta, 6.0f);
        else if (cmax == g)
            h = 60.0f * ((b - r) / delta + 2.0f);
        else
            h = 60.0f * ((r - g) / delta + 4.0f);
    }
    if (h < 0.0f) { h += 360.0f; }
    
    float s = (cmax > 0.0f) ? (delta / cmax) : 0.0f;
    float v = cmax;
    
    return float3(h / 360.0f, s, v);
}

float3 HSVtoRGB(float3 hsv)
{
    float h = hsv.x * 360.0f;
    float s = hsv.y;
    float v = hsv.z;

    float c = v * s;
    float x = c * (1.0f - abs(fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float3 rgb;
    if (h < 60.0f)
        rgb = float3(c, x, 0);
    else if (h < 120.0f)
        rgb = float3(x, c, 0);
    else if (h < 180.0f)
        rgb = float3(0, c, x);
    else if (h < 240.0f)
        rgb = float3(0, x, c);
    else if (h < 300.0f)
        rgb = float3(x, 0, c);
    else
        rgb = float3(c, 0, x);

    return rgb + m;
}

float4 WorldAlignedTexture(Texture2D tex, float3 worldPosition, float3 worldNormal, float3 textureSize, float blendSharpness)
{
    float2 uvX = worldPosition.zy / textureSize.x;
    float2 uvY = worldPosition.xz / textureSize.y;
    float2 uvZ = worldPosition.xy / textureSize.z;

    float4 sX = tex.Sample(gsamPointClamp, uvX);
    float4 sY = tex.Sample(gsamPointClamp, uvY);
    float4 sZ = tex.Sample(gsamPointClamp, uvZ);

    float3 weights = pow(abs(worldNormal), blendSharpness);
    weights /= weights.x + weights.y + weights.z;

    return sX * weights.x + sY * weights.y + sZ * weights.z;
}

#endif