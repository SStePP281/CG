Texture2D gDepth : register(t0);
Texture2D<uint2> gStencil : register(t1);
struct Hit
{
    float3 PosW;
    uint Valid;
    float2 MapUV;
    float2 Pad;
};
StructuredBuffer<Hit> gHit : register(t2);

SamplerState gsamPointClamp : register(s1);

cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    
    float4x4 gProj;
    float4x4 gInvProj;
    
    float4x4 gViewProj;
    float4x4 gJitteredViewProj;
    float4x4 gInvViewProj;
    float4x4 gPrevViewProj;
    
    float3 gEyePosW;
    float cbPerObjectPad1;
    
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;

    int DebugMode;
    int DebugViewIndex;
    int DebugUseBeckmann;
    int TerrainDebug;
};

cbuffer cbBrushRing : register(b1)
{
    float gBrushRadius;
    float gBrushMode;
    float gBrushStrength;
    float gRingPad;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

float3 ReconstructPosition(float2 texCoord, float depth)
{
    float x = texCoord.x * 2.0f - 1.0f;
    float y = (1.0f - texCoord.y) * 2.0f - 1.0f;

    float4 ndcPos = float4(x, y, depth, 1.0f);
    float4 worldPos = mul(ndcPos, gInvViewProj);

    return worldPos.xyz / worldPos.w;
}

VSOut VS(uint vid : SV_VertexID)
{
    VSOut vout;
    vout.TexC = float2((vid << 1) & 2, vid & 2);
    vout.PosH = float4(vout.TexC * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return vout;
}

float4 PS(VSOut pin) : SV_Target
{
    if (gBrushRadius <= 0.0f) { discard; }

    Hit hit = gHit[0];
    if (hit.Valid == 0) { discard; }

    float depth = gDepth.Sample(gsamPointClamp, pin.TexC).r;
    if (depth >= 1.0f) { discard; }

    float3 posW = ReconstructPosition(pin.TexC, depth);

    float d = distance(posW.xz, hit.PosW.xz);
    float w = min(fwidth(d), gBrushRadius * 0.05f) * 1.5f;
    float3 color = (gBrushMode > 0.5f) ? float3(1.0f, 0.25f, 0.25f) : float3(0.25f, 1.0f, 0.35f);

    float ring = 1.0f - smoothstep(0.0f, w, abs(d - gBrushRadius));
    float disc = (d < gBrushRadius) ? lerp(0.05f, 0.4f, gBrushStrength) : 0.0f;

    uint sw, sh;
    gStencil.GetDimensions(sw, sh);
    uint stencil = gStencil.Load(int3(pin.TexC * float2(sw, sh), 0)).g;
    float onTerrain = (stencil == 1) ? 1.0f : 0.0f;

    float alpha = saturate(ring + disc) * onTerrain;
    if (alpha <= 0.0f) { discard; }

    return float4(color, alpha);
}