#include "LightingUtil.hlsl"
#include "GBufferCommon.hlsl"

Texture2D gDiffuseMap : register(t0);
Texture2D gNormalMap : register(t1);
Texture2D gDispMap : register(t2);
Texture2D gMetallicMap : register(t3);
Texture2D gRoughnessMap : register(t4);
Texture2D gAOMap : register(t5);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

struct InstanceData
{
    float4x4 World;
    float4x4 TexTransform;
};

StructuredBuffer<InstanceData> gInstanceData : register(t3, space1);

struct VertexIn
{
    float3 PosL : POSITION;
    float3 NormalL : NORMAL;
    float4 TangentL : TANGENT;
    float2 TexC : TEXCOORD;
};

struct GBufferOutput
{
    float4 Albedo : SV_Target0;
    float4 Normal : SV_Target1;
    float4 MatData : SV_Target2; // R=metallic, G=roughness, B=AO
};

VertexOut VS(VertexIn vin, uint instanceID : SV_InstanceID)
{
    VertexOut vout;
    
    InstanceData instData = gInstanceData[instanceID];
    
    float4 posW = mul(float4(vin.PosL, 1.0f), instData.World);
    vout.PosW = posW.xyz;
    
    vout.NormalW = normalize(mul(vin.NormalL, (float3x3) instData.World));
    vout.TangentW = float4(normalize(mul(vin.TangentL.xyz, (float3x3) instData.World)), vin.TangentL.w);

    vout.PosH = mul(posW, gViewProj);
    
    float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), instData.TexTransform);
    vout.TexC = mul(texC, gMatTransform).xy;
    
    return vout;
}

GBufferOutput PS(DS_OUTPUT pin)
{
    GBufferOutput res;

    float4 albedoTex = gDiffuseMap.Sample(gsamAnisotropicWrap, pin.TexC);
    clip(albedoTex.a - 0.1f);
    res.Albedo = gDiffuseAlbedo * albedoTex;

    float3 normalSample = gNormalMap.Sample(gsamAnisotropicWrap, pin.TexC).xyz;
    float3 normalT = normalSample * 2.0f - 1.0f;
    normalT.xy *= gNormalIntencity;

    float3 N = normalize(pin.NormalW);
    float3 T = normalize(pin.TangentW.xyz);
    float3 B = normalize(cross(N, T) * pin.TangentW.w);

    float3x3 TBN = float3x3(T, B, N);
    float3 normalW = normalize(mul(normalT, TBN));
    res.Normal = float4(normalW, 1.0f);

    float metallic = gMetallicMap.Sample(gsamAnisotropicWrap, pin.TexC).r;
    float roughness = gRoughnessMap.Sample(gsamAnisotropicWrap, pin.TexC).r;
    float ao = gAOMap.Sample(gsamAnisotropicWrap, pin.TexC).r;

    metallic *= gMetallic;
    roughness *= gRoughness;

    roughness = clamp(roughness, 0.04f, 1.0f);
    metallic = saturate(metallic);
    ao = saturate(ao);

    res.MatData = float4(metallic, roughness, ao, 1.0f);

    return res;
}