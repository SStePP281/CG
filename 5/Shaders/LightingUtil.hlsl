#define LIGHT_TYPE_DIRECTION 0
#define LIGHT_TYPE_POINT     1
#define LIGHT_TYPE_SPOT      2

#define CASCADES_COUNT 3

static const float PI = 3.14159265359f;

struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
    int LightType;
    int3 Pad;
};

struct Material
{
    float3 Albedo;
    float Roughness;
    float Metallic;
    float AO;
};

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;

    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;

    float denom = NdotH2 * (a2 - 1.0f) + 1.0f;
    denom = PI * denom * denom;

    return a2 / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;

    return NdotV / (NdotV * (1.0f - k) + k);
}

float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);

    float ggx1 = GeometrySchlickGGX(NdotV, roughness);
    float ggx2 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

float3 CookTorrance(float3 N, float3 V, float3 L, float3 lightRadiance, Material mat)
{
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), mat.Albedo, mat.Metallic);

    float3 H = normalize(V + L);

    float NdotL = max(dot(N, L), 0.0f);

    if (NdotL <= 0.0f)
        return float3(0.0f, 0.0f, 0.0f);

    float D = DistributionGGX(N, H, mat.Roughness);
    float G = GeometrySmith(N, V, L, mat.Roughness);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);

    float3 numerator = D * G * F;
    float denominator = 4.0f * max(dot(N, V), 0.0f) * NdotL + 0.0001f;
    float3 specular = numerator / denominator;

    float3 kS = F;
    float3 kD = (float3(1.0f, 1.0f, 1.0f) - kS) * (1.0f - mat.Metallic);

    float3 diffuse = kD * mat.Albedo / PI;

    return (diffuse + specular) * lightRadiance * NdotL;
}

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

float3 ComputeDirectionalLight(Light L, Material mat, float3 N, float3 V)
{
    float3 lightDir = -L.Direction;
    float3 lightRadiance = L.Strength;

    return CookTorrance(N, V, lightDir, lightRadiance, mat);
}

float3 ComputePointLight(Light L, Material mat, float3 posW, float3 N, float3 V)
{
    float3 dVec = L.Position - posW;
    float dist = length(dVec);

    if (dist > L.FalloffEnd)
        return float3(0.0f, 0.0f, 0.0f);

    float3 L_dir = dVec / dist;
    float att = CalcAttenuation(dist, L.FalloffStart, L.FalloffEnd);

    float3 lightRadiance = L.Strength * att;

    return CookTorrance(N, V, L_dir, lightRadiance, mat);
}

float3 ComputeSpotLight(Light L, Material mat, float3 posW, float3 N, float3 V)
{
    float3 dVec = L.Position - posW;
    float dist = length(dVec);

    if (dist > L.FalloffEnd)
        return float3(0.0f, 0.0f, 0.0f);

    float3 L_dir = dVec / dist;
    float att = CalcAttenuation(dist, L.FalloffStart, L.FalloffEnd);
    float spotFactor = pow(max(dot(-L_dir, L.Direction), 0.0f), L.SpotPower);

    float3 lightRadiance = L.Strength * att * spotFactor;

    return CookTorrance(N, V, L_dir, lightRadiance, mat);
}