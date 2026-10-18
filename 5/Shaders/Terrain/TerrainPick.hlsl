Texture2D<float> gHeightMap : register(t0);
SamplerState gsamLinearClamp : register(s0);

cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
};

cbuffer cbPick : register(b1)
{
    float2 gMouseNdc;
    float gMaxDist;
    float gHeightScale;

    float gHeightOffset;
    float gWorldSize;
    float2 gWorldOffset;
};

struct Hit
{
    float3 PosW;
    uint Valid;
    float2 MapUV;
    float2 Pad;
};
RWStructuredBuffer<Hit> gHit : register(u0);

float2 ToUV(float3 p)
{
    return (p.xz - gWorldOffset) / gWorldSize;
}

float HeightAt(float2 uv)
{
    return gHeightMap.SampleLevel(gsamLinearClamp, uv, 0) * gHeightScale + gHeightOffset;
}

[numthreads(1, 1, 1)]
void CS()
{
    float4 n = mul(float4(gMouseNdc, 0, 1), gInvViewProj);
    float4 f = mul(float4(gMouseNdc, 1, 1), gInvViewProj);
    n /= n.w;
    f /= f.w;

    float3 origin = n.xyz;
    float3 dir = normalize(f.xyz - n.xyz);

    Hit hit = (Hit) 0;

    float prevT = 0.0f;
    for (float t = 0.0f; t < gMaxDist; t += 1.0f)
    {
        float3 p = origin + dir * t;
        float2 uv = ToUV(p);

        if (all(uv >= 0.0f) && all(uv <= 1.0f) && p.y < HeightAt(uv))
        {
            float lo = prevT;
            float hi = t;
            for (int k = 0; k < 8; k++)
            {
                float mid = 0.5f * (lo + hi);
                float3 pm = origin + dir * mid;
                if (pm.y < HeightAt(ToUV(pm)))
                {
                    hi = mid;
                }
                else
                {
                    lo = mid;
                }
            }

            float3 ph = origin + dir * hi;
            hit.PosW = ph;
            hit.MapUV = ToUV(ph);
            hit.Valid = 1;
            break;
        }

        prevT = t;
    }

    gHit[0] = hit;
}