Texture2D gPreviousColor : register(t0);
Texture2D gCurrentColor : register(t1);

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

float4 PS(VSOut pin) : SV_Target
{
    float3 prev = gPreviousColor.Sample(gsamPointClamp, pin.TexC).rgb;
    float3 current = gCurrentColor.Sample(gsamPointClamp, pin.TexC).rgb;
    
    return float4(lerp(prev, current, 0.1f), 1.0f);
}