struct InstanceData
{
    float4x4 World;
    float4x4 TexTransform;
};

StructuredBuffer<InstanceData> gInstanceData : register(t0, space1);

cbuffer cbShadow : register(b2)
{
    float4x4 gShadowViewProj[3];
    float4x4 gShadowTransform[3];
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
};

VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
    VertexOut vout;

    InstanceData instData = gInstanceData[instanceID];

    float4 posW = mul(float4(vin.PosL, 1.0f), instData.World);
    vout.PosH = mul(posW, gLightViewProj);

    return vout;
}
