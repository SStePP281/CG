#ifndef COMMON_HLSLI
#define COMMON_HLSLI

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

    int DebugUseBeckmann;
    int TerrainDebug;
    int gViewMode;
    int Pad;
};

#define VIEWMODE_LIT             0
#define VIEWMODE_UNLIT           1
#define VIEWMODE_WIREFRAME       2
#define VIEWMODE_LIGHTING_ONLY   3

float3 ReconstructPosition(float2 texCoord, float depth)
{
    float x = texCoord.x * 2.0f - 1.0f;
    float y = (1.0f - texCoord.y) * 2.0f - 1.0f;

    float4 ndcPos = float4(x, y, depth, 1.0f);
    float4 worldPos = mul(ndcPos, gInvViewProj);

    return worldPos.xyz / worldPos.w;
}

struct FullscreenVSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

FullscreenVSOut FullscreenTriangle(uint vid)
{
    FullscreenVSOut vout;
    vout.TexC = float2((vid << 1) & 2, vid & 2);
    vout.PosH = float4(vout.TexC * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
    return vout;
}

#endif // COMMON_HLSLI
