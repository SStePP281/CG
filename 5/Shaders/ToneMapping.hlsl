Texture2D<float4> gHDR : register(t0);

SamplerState gsamPointClamp : register(s1);

static const float EXPOSURE = 0.8f;

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
    float3 hdr = gHDR.Sample(gsamPointClamp, pin.TexC).rgb;

    float3 ldr = ToneMapReinhard(hdr, EXPOSURE);

    return float4(ldr, 1.0f);
}