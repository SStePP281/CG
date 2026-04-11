#include "LightingUtil.hlsl"

Texture2D gAlbedo : register(t0);
Texture2D gNormal : register(t1);
Texture2D gDepth : register(t2);

SamplerState gsamPointWrap : register(s0);
SamplerState gsamPointClamp : register(s1);
SamplerState gsamLinearWrap : register(s2);
SamplerState gsamLinearClamp : register(s3);
SamplerState gsamAnisotropicWrap : register(s4);
SamplerState gsamAnisotropicClamp : register(s5);

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

StructuredBuffer<Light> gLights : register(t3);

cbuffer cbLightInfo : register(b1)
{
    uint gLightCount;
    int3 pad;
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
    float2 uv = pin.TexC;
    
    float depth = gDepth.Sample(gsamPointClamp, uv).r;
    if (depth >= 1.0f)
        discard;
    
    float3 posW = ReconstructPosition(uv, depth);
    float4 albedo = gAlbedo.Sample(gsamPointClamp, uv);
    
    float3 normal = normalize(gNormal.Sample(gsamPointClamp, uv).xyz);
    float3 toEye = normalize(gEyePosW - posW);

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
                
            default:
                return float4(1.0f, 0.0f, 1.0f, 1.0f);
        }
    }

    float3 r0 = { 0.5f, 0.5f, 0.5f };
    Material mat = { albedo, r0, 0.5f };
    float3 lighting = 0;
    for (uint i = 0; i < gLightCount; ++i)
    {
        Light L = gLights[i];
        if (L.LightType == 0)
            lighting += ComputeDirectionalLight(L, mat, normal, toEye);
        else if (L.LightType == 1)
            lighting += ComputePointLight(L, mat, posW, normal, toEye);
        else if (L.LightType == 2)
            lighting += ComputeSpotLight(L, mat, posW, normal, toEye);
    }
    
    return float4(albedo.rgb * gAmbientLight.rgb + lighting.rgb, 1.0f);
}