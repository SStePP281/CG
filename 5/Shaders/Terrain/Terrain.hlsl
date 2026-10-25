Texture2D gHeightMap : register(t0);
Texture2DArray gColorMaps : register(t1);
Texture2DArray gMaskMaps : register(t2);

struct Hit
{
    float3 PosW;
    uint Valid;
    float2 MapUV;
    float2 Pad;
};
StructuredBuffer<Hit> gHit : register(t3);

SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);

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

cbuffer cbTerrain : register(b4)
{
    float2 gTileOrigin;
    float gTileSize;
    float gHeightScale;
    
    float2 gWorldSize;
    float2 gWorldOffset;
    
    float gHeightOffset;
    float gMorphStart;
    float gMorphEnd;
    float gTileCells;
    
    int gLodLevel;
    int gMaskCount;
    int gLayerCount;
    float gBrushRadius;

    float gBrushMode;
    float gBrushStrength;
    float2 gPad2;
    
    float4 gLayerTile[4];
};

static const float3 LOD_COLORS[6] =
{
    float3(1.0, 0.1, 0.1),
    float3(1.0, 0.6, 0.1),
    float3(1.0, 1.0, 0.1),
    float3(0.1, 1.0, 0.1),
    float3(0.1, 0.6, 1.0),
    float3(0.6, 0.2, 1.0)
};

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float4 TangentL : TANGENT;
    float2 TexC : TEXCOORD;
};

struct VertexOut
{
    float4 PosH : SV_POSITION;
    float4 CurrPosH : TEXCOORD2;
    float4 PrevPosH : TEXCOORD3;
    float3 PosW : POSITION;
    float2 TexC : TEXCOORD0;
    nointerpolation int Lod : TEXCOORD1;
};

struct GBufferOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float4 MatData : SV_Target2;
    float2 Velocity : SV_Target3;
};

float2 CalcVelocity(float4 currPosH, float4 prevPosH)
{
    float2 curr = currPosH.xy / currPosH.w;
    float2 prev = prevPosH.xy / prevPosH.w;
    return (prev - curr) * float2(0.5f, -0.5f);
}

VertexOut VS(VertexIn vin)
{
    VertexOut vout;
    
    float2 gridPos = vin.PosL.xz;
    
    float2 xz0 = gTileOrigin + gridPos * gTileSize;
    float2 uv0 = xz0 / gWorldSize;
    float h0 = gHeightMap.SampleLevel(gsamLinearClamp, uv0, 0).r * gHeightScale + gHeightOffset;
    float3 pos0 = float3(xz0.x + gWorldOffset.x, h0, xz0.y + gWorldOffset.y);
    
    float d = distance(gEyePosW, pos0);
    float morphK = saturate((d - gMorphStart) / (gMorphEnd - gMorphStart));
    
    float2 fracPart = frac(gridPos * (gTileCells * 0.5f)) * (2.0f / gTileCells);
    float2 gridMorphed = gridPos - fracPart * morphK;
    
    float2 localXZ = gTileOrigin + gridMorphed * gTileSize;
    float2 uv = localXZ / gWorldSize;
    float h = gHeightMap.SampleLevel(gsamLinearClamp, uv, 0).r * gHeightScale + gHeightOffset;

    vout.PosW = float3(localXZ.x + gWorldOffset.x, h, localXZ.y + gWorldOffset.y);
    vout.PosH = mul(float4(vout.PosW, 1), gJitteredViewProj);
    vout.CurrPosH = mul(float4(vout.PosW, 1), gViewProj);
    vout.PrevPosH = mul(float4(vout.PosW, 1), gPrevViewProj);
    vout.TexC = uv;
    vout.Lod = gLodLevel;
    
    return vout;
}

GBufferOutput PS(VertexOut pin)
{
    GBufferOutput res;
    
    uint w, h;
    gHeightMap.GetDimensions(w, h);
    
    float2 texel = float2(1.0 / w, 1.0 / h);
    float step = gWorldSize.x / w;
    
    float hL = gHeightMap.SampleLevel(gsamLinearClamp, pin.TexC - float2(texel.x, 0), 0).r * gHeightScale;
    float hR = gHeightMap.SampleLevel(gsamLinearClamp, pin.TexC + float2(texel.x, 0), 0).r * gHeightScale;
    float hD = gHeightMap.SampleLevel(gsamLinearClamp, pin.TexC - float2(0, texel.y), 0).r * gHeightScale;
    float hU = gHeightMap.SampleLevel(gsamLinearClamp, pin.TexC + float2(0, texel.y), 0).r * gHeightScale;
    
    float3 N = normalize(float3(hL - hR, 2.0 * step, hD - hU));
    
    float2 mapMeters = pin.TexC * gWorldSize;
    float3 albedo = 0;

    for (int m = 0; m < gMaskCount; m++)
    {
        float4 weights = gMaskMaps.Sample(gsamLinearClamp, float3(pin.TexC, m));

        for (int k = 0; k < 4; k++)
        {
            int layer = m * 4 + k;
            if (layer < gLayerCount)
            {
                float tile = gLayerTile[layer / 4][layer % 4];
                float2 tuv = mapMeters / tile;
                albedo += weights[k] * gColorMaps.Sample(gsamAnisotropicWrap, float3(tuv, layer)).rgb;
            }
        }
    }
    
    res.Albedo = float4(albedo, 1);
    res.Normal = float4(N, 1);
    res.MatData = float4(0, 0.9, 1, 1);

    if (TerrainDebug == 1)
    {
        uint lod = min((uint) pin.Lod, 5u);
        res.Albedo.rgb = lerp(res.Albedo.rgb, LOD_COLORS[lod], 0.6f);
    }

    if (gBrushRadius > 0.0f)
    {
        Hit hit = gHit[0];
        if (hit.Valid != 0)
        {
            float d = distance(pin.PosW.xz, hit.PosW.xz);
            float3 brushColor = (gBrushMode > 0.5f) ? float3(1.0f, 0.25f, 0.25f) : float3(0.25f, 1.0f, 0.35f);

            float ring = 1.0f - smoothstep(0.0f, fwidth(d) * 1.5f, abs(d - gBrushRadius));
            float disc = (d < gBrushRadius) ? lerp(0.05f, 0.4f, gBrushStrength) : 0.0f;

            res.Albedo.rgb = lerp(res.Albedo.rgb, brushColor, saturate(ring + disc));
        }
    }
    
    res.Velocity = CalcVelocity(pin.CurrPosH, pin.PrevPosH);

    return res;
}