Texture2D gPreviousColor : register(t0);
Texture2D gCurrentColor : register(t1);
Texture2D gPreviousVelocity : register(t2);
Texture2D gCurrentVelocity : register(t3);
Texture2D gDepth : register(t4);

SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearClamp : register(s3);

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

float3 SampleCatmullRom(Texture2D tex, float2 uv, float2 texSize)
{
    float2 samplePos = uv * texSize;
    float2 texPos1 = floor(samplePos - 0.5f) + 0.5f;
    float2 f = samplePos - texPos1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12 = w1 + w2;
    float2 offset12 = w2 / w12;

    float2 texPos0 = (texPos1 - 1.0f) / texSize;
    float2 texPos3 = (texPos1 + 2.0f) / texSize;
    float2 texPos12 = (texPos1 + offset12) / texSize;

    float3 result = 0.0f;
    result += tex.SampleLevel(gsamLinearClamp, float2(texPos0.x, texPos0.y), 0).rgb * w0.x * w0.y;
    result += tex.SampleLevel(gsamLinearClamp, float2(texPos12.x, texPos0.y), 0).rgb * w12.x * w0.y;
    result += tex.SampleLevel(gsamLinearClamp, float2(texPos3.x, texPos0.y), 0).rgb * w3.x * w0.y;

    result += tex.SampleLevel(gsamLinearClamp, float2(texPos0.x, texPos12.y), 0).rgb * w0.x * w12.y;
    result += tex.SampleLevel(gsamLinearClamp, float2(texPos12.x, texPos12.y), 0).rgb * w12.x * w12.y;
    result += tex.SampleLevel(gsamLinearClamp, float2(texPos3.x, texPos12.y), 0).rgb * w3.x * w12.y;

    result += tex.SampleLevel(gsamLinearClamp, float2(texPos0.x, texPos3.y), 0).rgb * w0.x * w3.y;
    result += tex.SampleLevel(gsamLinearClamp, float2(texPos12.x, texPos3.y), 0).rgb * w12.x * w3.y;
    result += tex.SampleLevel(gsamLinearClamp, float2(texPos3.x, texPos3.y), 0).rgb * w3.x * w3.y;

    return max(result, 0.0f);
}

float LuminanceWeight(float3 color)
{
    float luminance = dot(color, float3(0.299f, 0.587f, 0.114f));
    return 1.0f / (1.0f + luminance);
}

float4 PS(VSOut pin) : SV_Target
{
    //Color Clamp
    uint w, h;
    gCurrentColor.GetDimensions(w, h);
    float2 texel = 1.0f / float2(w, h);
    
    float3 cSum = 0.0f;
    float closestDepth = 100.0f;
    float2 closestUVOffset = 0.0f;
    float3 cMin = 99999.0f, cMax = -99999.0f;
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            float3 c = gCurrentColor.Sample(gsamPointClamp, pin.TexC + float2(x, y) * texel).rgb;
            cSum += c;
            
            cMin = min(cMin, c);
            cMax = max(cMax, c);
            
            //Depth Dilation
            float2 uvOffset = float2(x, y) * texel;
            float neighborDepth = gDepth.Sample(gsamPointClamp, pin.TexC + uvOffset).r;
            if (neighborDepth < closestDepth)
            {
                closestDepth = neighborDepth;
                closestUVOffset = uvOffset;
            }
        }
    }
    float3 blurredCurrentColor = cSum / 9.0f;
    
    
    //Depth Dilation
    float2 curVel = gCurrentVelocity.Sample(gsamPointClamp, pin.TexC + closestUVOffset).xy;
    
    float2 reprojectedUV = pin.TexC + curVel;
    
    //Catmull-Rom Filtering
    float3 prev = SampleCatmullRom(gPreviousColor, reprojectedUV, float2(w, h));
    float3 current = gCurrentColor.Sample(gsamPointClamp, pin.TexC).rgb;
    
    if (any(reprojectedUV < 0.0f) || any(reprojectedUV > 1.0f)) 
        return float4(current, 1.0f);
    
    //ColorClamp
    prev = clamp(prev, cMin, cMax);
    
    //Velocity Rejection
    float2 prevReprojectedVel = gPreviousVelocity.Sample(gsamPointClamp, reprojectedUV).xy;
    float velLength = length(prevReprojectedVel - curVel);
    float velDisocclusion = saturate((velLength - 0.001f) * 10.0f);
    
    //Luminance Flickering
    float wCur = 0.1f * LuminanceWeight(current);
    float wPrev = 0.9f * LuminanceWeight(prev);
    float3 accumulation = (current * wCur + prev * wPrev) / (wCur + wPrev);
    
    return float4(lerp(accumulation, blurredCurrentColor, velDisocclusion), 1.0f);
}