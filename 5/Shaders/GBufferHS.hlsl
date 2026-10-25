#include "GBufferCommon.hlsl"

HS_CONSTANT_DATA_OUTPUT ConstantsHS(InputPatch<VertexOut, 3> patch, uint PatchID : SV_PrimitiveID);

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("ConstantsHS")]
[maxtessfactor(16.0f)]
HS_CONTROL_POINT_OUTPUT HS(InputPatch<VertexOut, 3> inputPatch, uint uCPID : SV_OutputControlPointID)
{
    HS_CONTROL_POINT_OUTPUT Out;

    Out.PosW = inputPatch[uCPID].PosW;
    Out.PrevPosW = inputPatch[uCPID].PrevPosW;
    Out.NormalW = inputPatch[uCPID].NormalW;
    Out.TangentW = inputPatch[uCPID].TangentW;
    Out.TexC = inputPatch[uCPID].TexC;

    return Out;
}

float TessEdge(float3 v0, float3 v1)
{
    float3 mid = 0.5f * (v0 + v1);
    float dist = distance(mid, gEyePosW);

    float t = saturate(dist / min(gMaxTessellationDistance, g_MaxTessellationDistance));
    float tess = lerp(min(gTessellationFactor, gMaxTessellationFactor), 1.0f, t);

    return max(1.0f, tess);
}

HS_CONSTANT_DATA_OUTPUT ConstantsHS(InputPatch<VertexOut, 3> p, uint pid : SV_PrimitiveID)
{
    HS_CONSTANT_DATA_OUTPUT Out;

    float e0 = TessEdge(p[1].PosW, p[2].PosW);
    float e1 = TessEdge(p[2].PosW, p[0].PosW);
    float e2 = TessEdge(p[0].PosW, p[1].PosW);

    Out.Edges[0] = e0;
    Out.Edges[1] = e1;
    Out.Edges[2] = e2;
    Out.Inside = (e0 + e1 + e2) / 3.0f;
    
    return Out;
}