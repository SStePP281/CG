#include "GBufferCommon.hlsl"

Texture2D gDisplacementMap : register(t2);
SamplerState gsamAnisotropicWrap : register(s4);

[domain("tri")]
DS_OUTPUT DS(HS_CONSTANT_DATA_OUTPUT input, float3 Barycentric : SV_DomainLocation, const OutputPatch<HS_CONTROL_POINT_OUTPUT, 3> TrianglePatch)
{
    DS_OUTPUT Out;

    Out.PosW = 
            Barycentric.x * TrianglePatch[0].PosW +
            Barycentric.y * TrianglePatch[1].PosW +
            Barycentric.z * TrianglePatch[2].PosW;

    float3 prevPosW =
            Barycentric.x * TrianglePatch[0].PrevPosW +
            Barycentric.y * TrianglePatch[1].PrevPosW +
            Barycentric.z * TrianglePatch[2].PrevPosW;

    Out.NormalW = normalize(
            Barycentric.x * TrianglePatch[0].NormalW +
            Barycentric.y * TrianglePatch[1].NormalW +
            Barycentric.z * TrianglePatch[2].NormalW);

    Out.TangentW = float4(normalize(
            Barycentric.x * TrianglePatch[0].TangentW.xyz +
            Barycentric.y * TrianglePatch[1].TangentW.xyz +
            Barycentric.z * TrianglePatch[2].TangentW.xyz),
            TrianglePatch[0].TangentW.w);

    Out.TexC = 
            Barycentric.x * TrianglePatch[0].TexC +
            Barycentric.y * TrianglePatch[1].TexC +
            Barycentric.z * TrianglePatch[2].TexC;
    
    float h = gDisplacementMap.SampleLevel(gsamAnisotropicWrap, Out.TexC, 0).r;
    h = h * gDisplacementScale + gDisplacementBias;
    
    Out.PosW += Out.NormalW * h;
    prevPosW += Out.NormalW * h;

    Out.PosH = mul(float4(Out.PosW, 1.0f), gJitteredViewProj);
    Out.CurrPosH = mul(float4(Out.PosW, 1.0f), gViewProj);
    Out.PrevPosH = mul(float4(prevPosW, 1.0f), gPrevViewProj);
    
    return Out;
}