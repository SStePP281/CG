// Buffer visualization (ViewMode::BufferVisualization). One fullscreen triangle per tile:
// the C++ side sets the viewport (whole screen, or one cell of the Overview grid), so each tile shows the whole frame.
//
// Root signature (BufferVisualizer.cpp):
//   b0  cbPass                        (Common.hlsli)
//   b1  root constant: buffer index
//   b2  cbShadow                      (Shadow.hlsli)
//   t0-t4  G-buffer table: Albedo, Normal, MatData, Depth, Stencil (contiguous in GBuffer::_srvRange)
//   t5  shadow map array              (Shadow.hlsli)
//   t6  velocity
//   s1  point clamp, s6 shadow comparison (Shadow.hlsli)

#include "../Common.hlsli"
#include "../Shadow.hlsli"

// Must match enum class DebugBuffer in RenderView.h (0 = Overview is a layout, never sent here).
#define VIEW_BASE_COLOR 1
#define VIEW_NORMAL     2
#define VIEW_METALLIC   3
#define VIEW_ROUGHNESS  4
#define VIEW_AO         5
#define VIEW_DEPTH      6
#define VIEW_VELOCITY   7
#define VIEW_SHADOW     8
#define VIEW_CASCADES   9

Texture2D gAlbedo : register(t0);
Texture2D gNormal : register(t1);
Texture2D gMatData : register(t2); // metallic, roughness, AO
Texture2D gDepth : register(t3);
Texture2D<uint2> gStencil : register(t4);

Texture2D gVelocity : register(t6);

SamplerState gsamPointClamp : register(s1);

cbuffer cbView : register(b1)
{
    uint gBufferIndex;
};

static const float3 SKY_COLOR = float3(0.0f, 0.0f, 0.0f);

// Linear view depth squeezed into 0..1; sqrt spreads near distances, otherwise everything close is black.
float VisualizeDepth(float viewZ)
{
    return sqrt(saturate((viewZ - gNearZ) / (gFarZ - gNearZ)));
}

FullscreenVSOut VS(uint vid : SV_VertexID)
{
    return FullscreenTriangle(vid);
}

float4 PS(FullscreenVSOut pin) : SV_Target
{
    float2 uv = pin.TexC;

    if (gBufferIndex == VIEW_VELOCITY)
    {
        float2 v = gVelocity.Sample(gsamPointClamp, uv).xy;
        return float4(abs(v) * 100.0f, 0.0f, 1.0f);
    }

    float depth = gDepth.Sample(gsamPointClamp, uv).r;
    if (depth >= 1.0f) { return float4(SKY_COLOR, 1.0f); }   // tiles are opaque: paint the sky instead of discarding

    float3 posW = ReconstructPosition(uv, depth);
    float viewZ = mul(float4(posW, 1.0f), gView).z;

    float3 albedo = gAlbedo.Sample(gsamPointClamp, uv).rgb;
    float3 N = normalize(gNormal.Sample(gsamPointClamp, uv).xyz);
    float4 matData = gMatData.Sample(gsamPointClamp, uv);

    switch (gBufferIndex)
    {
        case VIEW_BASE_COLOR: return float4(albedo, 1.0f);
        case VIEW_NORMAL:     return float4(N * 0.5f + 0.5f, 1.0f);
        case VIEW_METALLIC:   return float4(matData.rrr, 1.0f);
        case VIEW_ROUGHNESS:  return float4(matData.ggg, 1.0f);
        case VIEW_AO:         return float4(matData.bbb, 1.0f);
        case VIEW_DEPTH:      return float4(VisualizeDepth(viewZ).xxx, 1.0f);
        case VIEW_SHADOW:     return float4(CalcShadow(posW, viewZ).xxx, 1.0f);

        case VIEW_CASCADES:
        {
            // Cascade color, darkened where the pixel is in shadow.
            float3 cascadeColor = CASCADES_COLORS[GetCascadesLayer(viewZ)];
            float shadow = CalcShadow(posW, viewZ);
            return float4(cascadeColor * lerp(0.25f, 1.0f, shadow), 1.0f);
        }
    }

    return float4(1.0f, 0.0f, 1.0f, 1.0f);   // unknown buffer: magenta
}
