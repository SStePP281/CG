#include "LightingUtil.hlsl"
#include "Common.hlsli"
#include "Shadow.hlsli"

static const float MAX_REFLECTION_LOD = 4.0f;

Texture2D gAlbedo : register(t0);
Texture2D gNormal : register(t1);
Texture2D gMatData : register(t2);
Texture2D gDepth : register(t3);

StructuredBuffer<Light> gLights : register(t4);

TextureCube gIrradianceMap : register(t6);
TextureCube gPrefilteredEnv : register(t7);
Texture2D gBrdfLut : register(t8);

Texture2D gVelocity : register(t9);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

cbuffer cbLightInfo : register(b1)
{
    uint gLightCount;
    float gIBLIntensity;
    uint gShadowsEnabled;
    int pad;
};

struct VSOut
{
    float4 PosH : SV_POSITION;
    float2 TexC : TEXCOORD;
};

float3 CalculatePBRLighting(float3 N, float3 V, float3 posW, Material mat, float shadowFactor)
{
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    for (uint i = 0; i < gLightCount; ++i)
    {
        Light L = gLights[i];

        if (L.LightType == LIGHT_TYPE_DIRECTION)
        {
            Lo += ComputeDirectionalLight(L, mat, N, V) * shadowFactor;
        }
        else if (L.LightType == LIGHT_TYPE_POINT)
        {
            Lo += ComputePointLight(L, mat, posW, N, V);
        }
        else if (L.LightType == LIGHT_TYPE_SPOT)
        {
            Lo += ComputeSpotLight(L, mat, posW, N, V);
        }
    }

    return Lo;
}

float3 CalculateIBLAmbient(float3 N, float3 V, Material mat)
{
    float3 albedo = mat.Albedo;
    float metallic = mat.Metallic;
    float roughness = mat.Roughness;
    float ao = mat.AO;
    
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 F = FresnelSchlickRoughness(max(dot(N, V), 0.0f), F0, roughness);
    float3 kD = (1.0f - F) * (1.0f - metallic);

    float3 irradiance = gIrradianceMap.Sample(gsamLinearClamp, N).rgb;
    float3 diffuseIBL = kD * irradiance * albedo;

    float3 R = reflect(-V, N);
    float3 prefilteredColor = gPrefilteredEnv.SampleLevel(
    gsamLinearClamp, R, roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = gBrdfLut.Sample(
    gsamLinearClamp, float2(max(dot(N, V), 0.0f), roughness)).rg;
    float3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);

    return (diffuseIBL + specularIBL) * ao;
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
    float2 uv = pin.TexC;

    float depth = gDepth.Sample(gsamPointClamp, uv).r;
    if (depth >= 1.0f)
        discard;

    float3 posW = ReconstructPosition(uv, depth);
    float4 posV = mul(float4(posW, 1.0f), gView);
    float viewZ = posV.z;

    float3 albedo = gAlbedo.Sample(gsamPointClamp, uv).rgb;
    float3 N = normalize(gNormal.Sample(gsamPointClamp, uv).xyz);
    float4 matData = gMatData.Sample(gsamPointClamp, uv);

    float metallic = matData.r;
    float roughness = matData.g;
    float ao = matData.b;

    if (gViewMode == VIEWMODE_UNLIT || gViewMode == VIEWMODE_WIREFRAME)
    {
        return float4(albedo, 1.0f);
    }

    float3 V = normalize(gEyePosW - posW);

    Material mat;
    mat.Albedo = albedo;
    mat.Roughness = roughness;
    mat.Metallic = metallic;
    mat.AO = ao;
    mat.UseBeckmann = DebugUseBeckmann;

    if (gViewMode == VIEWMODE_LIGHTING_ONLY)
    {
        // Grey clay: only light and shadow are visible.
        mat.Albedo = float3(0.5f, 0.5f, 0.5f);
        mat.Metallic = 0.0f;
    }

    float shadowFactor = gShadowsEnabled ? CalcShadow(posW, viewZ) : 1.0f;

    float3 Lo = CalculatePBRLighting(N, V, posW, mat, shadowFactor);

    float3 IBL = CalculateIBLAmbient(N, V, mat);

    float3 color = IBL * gIBLIntensity + Lo;

    return float4(color, 1.0f);
}