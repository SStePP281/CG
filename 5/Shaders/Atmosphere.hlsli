#ifndef ATMOSPHERE_HLSLI
#define ATMOSPHERE_HLSLI

static const float SUN_FADE_START = -0.05f;
static const float SUN_FADE_END = 0.02f;

cbuffer cbAtmosphere : register(b3)
{
    float3 gSunDir;
    float gSunIntensity;

    float3 gSunColor;
    float gMieG;

    float3 gBetaR;
    float gBetaM;

    float gHeightR;
    float gHeightM;
    float gMetersPerUnit;
    
    uint gAtmFlags;
    float gIBLIntensity;
    int3 APad;
};

#define ATM_ENABLED        1
#define ATM_AFFECT_OBJECTS 2

float PhaseRayleigh(float cosT)
{
    return 3.0f / (16.0f * PI) * (1.0f + cosT * cosT);
}

float PhaseMie(float cosT, float g)
{
    float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(1.0f + g2 - 2.0f * g * cosT, 1.5f));
}

float AirMass(float cosZenith)
{
    cosZenith = saturate(cosZenith);
    float a = degrees(acos(cosZenith));
    return 1.0f / (cosZenith + 0.15f * pow(93.885f - a, -1.253f));
}

float3 SunIrradiance()
{
    float m = AirMass(gSunDir.y);
    float3 tau = gBetaR * gHeightR * m + gBetaM * gHeightM * m;
    float aboveHorizon = smoothstep(SUN_FADE_START, SUN_FADE_END, gSunDir.y);
    return gSunColor * gSunIntensity * exp(-tau) * aboveHorizon;
}

float3 SkyColor(float3 viewDir)
{
    float cosT = dot(viewDir, gSunDir);
    float m = AirMass(viewDir.y);

    float3 inR = PhaseRayleigh(cosT) * (1.0f - exp(-gBetaR * gHeightR * m));
    float inM = PhaseMie(cosT, gMieG) * (1.0f - exp(-gBetaM * gHeightM * m));

    return SunIrradiance() * (inR + inM);
}

float3 ApplyAerialPerspective(float3 color, float3 posW, float3 eyePosW)
{
    float3 toPoint = posW - eyePosW;
    float s = length(toPoint) * gMetersPerUnit;
    float3 viewDir = toPoint / max(length(toPoint), 1e-4f);

    float cosT = dot(viewDir, gSunDir);

    float3 ext = max(gBetaR + gBetaM, 1e-12f);
    float3 Fex = exp(-ext * s);

    float3 scatter = gBetaR * PhaseRayleigh(cosT) + gBetaM * PhaseMie(cosT, gMieG);
    float3 Lin = scatter / ext * SunIrradiance() * (1.0f - Fex);

    return color * Fex + Lin;
}

#endif // ATMOSPHERE_HLSLI
