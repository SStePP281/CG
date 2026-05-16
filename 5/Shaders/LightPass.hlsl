#include "LightingUtil.hlsl"

static const float3 CASCADES_COLORS[CASCADES_COUNT] =
{
    { 1.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f }
};

Texture2D gAlbedo : register(t0);
Texture2D gNormal : register(t1);
Texture2D gDepth : register(t2);

StructuredBuffer<Light> gLights : register(t3);

Texture2DArray gShadowMap : register(t4);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);
SamplerComparisonState gsamShadow : register(s6);

cbuffer cbPass : register(b0)
{
    float4x4 gView;
    float4x4 gInvView;
    float4x4 gProj;
    float4x4 gInvProj;
    float4x4 gViewProj;
    float4x4 gInvViewProj;
    float3 gEyePosW;
    float cbPerObjectPad1;
    float2 gRenderTargetSize;
    float2 gInvRenderTargetSize;
    float gNearZ;
    float gFarZ;
    float gTotalTime;
    float gDeltaTime;
    float4 gAmbientLight;

    int DebugMode;
    int DebugViewIndex;
    int2 Pad;
};

cbuffer cbLightInfo : register(b1)
{
    uint gLightCount;
    int3 pad;
};

struct CascadeData
{
    float4x4 gLightViewProj;
    float4x4 gShadowTransform;
    float4 gDistances;
    float4 padding[7];
};

cbuffer cbShadow : register(b2)
{
    CascadeData gCascades[CASCADES_COUNT];
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

uint GetCascadesLayer(float viewZ)
{
    uint cascade = 0;
    
    [unroll]
    for (uint j = 0; j < CASCADES_COUNT - 1; j++)
    {
        if (viewZ > gCascades[0].gDistances[j])
        {
            cascade = j + 1;
        }
    }
    
    return min(cascade, (uint) (CASCADES_COUNT - 1));
}

float CalcShadow(float3 posW, float viewZ)
{
    uint cascade = GetCascadesLayer(viewZ);

    float4 shadowPos = mul(float4(posW, 1.0f), gCascades[cascade].gShadowTransform);
    shadowPos.xyz /= shadowPos.w;

    if (shadowPos.x < 0.0f || shadowPos.x > 1.0f || shadowPos.y < 0.0f || shadowPos.y > 1.0f)
    {
        return 1.0f;
    }

    float width, height, elements;
    gShadowMap.GetDimensions(width, height, elements);
    float2 dx = 1.0f / float2(width, height);

    float shadow = 0.0f;
    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            float2 offset = float2(x, y) * dx;
            shadow += gShadowMap.SampleCmpLevelZero(
                gsamShadow,
                float3(shadowPos.xy + offset, (float) cascade),
                shadowPos.z - 0.001f
            );
        }
    }
    return shadow / 9.0f;
}

float3 CalculateLightness(float3 normal, float3 toEye, float3 posW, Material mat, float shadowFactor)
{
    float3 lighting = 0;
    for (uint i = 0; i < gLightCount; ++i)
    {
        Light L = gLights[i];
        if (L.LightType == LIGHT_TYPE_DIRECTION)
        {
            lighting += ComputeDirectionalLight(L, mat, normal, toEye) * shadowFactor;
        }
        else if (L.LightType == LIGHT_TYPE_POINT)
        {
            lighting += ComputePointLight(L, mat, posW, normal, toEye);
        }
        else if (L.LightType == LIGHT_TYPE_SPOT)
        {
            lighting += ComputeSpotLight(L, mat, posW, normal, toEye);
        }
    }
    
    return lighting;
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
    {
        discard;
    }

    float3 posW = ReconstructPosition(uv, depth);
    float4 albedo = gAlbedo.Sample(gsamPointClamp, uv);
    float3 normal = normalize(gNormal.Sample(gsamPointClamp, uv).xyz);
    float3 toEye = normalize(gEyePosW - posW);

    float4 posV = mul(float4(posW, 1.0f), gView);
    float viewZ = posV.z;

    float3 r0 = { 0.5f, 0.5f, 0.5f };
    Material mat = { albedo, r0, 0.5f };
    
    if (DebugMode)
    {
        switch (DebugViewIndex)
        {
            case 0:
                break;
            case 1:
                return albedo;
            case 2:
                return float4(normal * 0.5f + 0.5f, 1.0f);
            case 3:
                return float4(depth.xxx, 1.0f);
            case 4:
                uint cascade = GetCascadesLayer(viewZ);
                float3 cascadeColor = CASCADES_COLORS[cascade];
                float shadowFactor = CalcShadow(posW, viewZ);

                float3 lighting = CalculateLightness(normal, toEye, posW, mat, shadowFactor);

                float3 resultColor = albedo.rgb * gAmbientLight.rgb + lighting.rgb;
   
                float shadowMask = 1.0f - shadowFactor;
                resultColor += cascadeColor * shadowMask * 0.3f;

                return float4(resultColor, 1.0f);
            default:
                return float4(1.0f, 0.0f, 1.0f, 1.0f);
        }
    }
    
    float shadowFactor = CalcShadow(posW, viewZ);
    float3 lighting = CalculateLightness(normal, toEye, posW, mat, shadowFactor);
    
    return float4(albedo.rgb * gAmbientLight.rgb + lighting.rgb, 1.0f);
}