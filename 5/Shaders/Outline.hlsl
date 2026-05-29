Texture2D gDepth : register(t0);
Texture2D gNormal : register(t1);
Texture2D gColor : register(t2);

SamplerState gsamPointClamp : register(s1);

cbuffer cbOutline : register(b0)
{
    float2 gTexelSize;
    float gKernelSize;
    float gDepthThreshold;
    float gNormalThreshold;
    float3 gOutlineColor;
};

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
    float2 uv = pin.TexC;
    
    float KERNEL_SIZE = floor(gKernelSize);
    if (KERNEL_SIZE < 2.0f)
    {
        return gColor.Sample(gsamPointClamp, uv);
    }

    float HALF_KERNEL_SIZE = floor(KERNEL_SIZE / 2.0f);
    float HALF_KERNEL_SIZE_SQ = KERNEL_SIZE * KERNEL_SIZE / 4.0f;

    float3 laplacianNormal = float3(0.0f, 0.0f, 0.0f);
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

    float normalEdge = length(laplacianNormal);
    float depthEdge = abs(laplacianDepth);

    float edge = step(gNormalThreshold, normalEdge) + step(gDepthThreshold, depthEdge);
    edge = saturate(edge);

    float4 color = gColor.Sample(gsamPointClamp, uv);
    
    return float4(lerp(color.rgb, gOutlineColor, edge), 1.0f);
}