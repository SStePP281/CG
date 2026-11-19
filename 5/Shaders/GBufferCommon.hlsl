struct VertexOut
{
    float4 PosH : SV_POSITION;
    float3 PosW : POSITION;
    float3 PrevPosW : TEXCOORD1;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 TexC : TEXCOORD0;
};

struct HS_CONTROL_POINT_OUTPUT
{
    float3 PosW : POSITION;
    float3 PrevPosW : TEXCOORD1;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 TexC : TEXCOORD0;
};

struct HS_CONSTANT_DATA_OUTPUT
{
    float Edges[3] : SV_TessFactor;
    float Inside : SV_InsideTessFactor;
};

struct DS_OUTPUT
{
    float4 PosH : SV_POSITION;
    float4 CurrPosH : TEXCOORD1;
    float4 PrevPosH : TEXCOORD2;
    float3 PosW : POSITION;
    float3 NormalW : NORMAL;
    float4 TangentW : TANGENT;
    float2 TexC : TEXCOORD0;
};

#include "Common.hlsli"

cbuffer cbMaterial : register(b1)
{
    float4x4 gMatTransform;

    float4 gDiffuseAlbedo;
    float gRoughness;
    float gMetallic;
    float gNormalIntencity;
    float gMatPad0;

    float gMaxTessellationFactor;
    float g_MaxTessellationDistance;
    float2 gMatPad1;
};

cbuffer cbTessellation : register(b2)
{
    float gTessellationFactor;
    float gMaxTessellationDistance;
    float2 gPad;
};

cbuffer cbDisplacement : register(b3)
{
    float gDisplacementScale;
    float gDisplacementBias;
    float2 g_Pad;
};