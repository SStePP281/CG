#define LIGHT_TYPE_DIRECTION 0
#define LIGHT_TYPE_POINT 1
#define LIGHT_TYPE_SPOT 2

#define CASCADES_COUNT 3

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
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
};

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd-d) / (falloffEnd - falloffStart));
}

float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0)*(f0*f0*f0*f0*f0);

    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);

    float roughnessFactor = (m + 8.0f)*pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);

    float3 specAlbedo = fresnelFactor*roughnessFactor;

    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;
}

float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye)
{
    float3 lightVec = -L.Direction;

    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    float3 dVec = L.Position - pos;
    float distSq = dot(dVec, dVec);
    float rangeSq = L.FalloffEnd * L.FalloffEnd;

    if (distSq > rangeSq)
        return 0.0f;

    float d = sqrt(distSq);
    float3 lightVec = dVec / d;

    float ndotl = max(dot(lightVec, normal), 0.0f);
    if (ndotl <= 0.0f)
        return 0.0f;

    float3 lightStrength = L.Strength * ndotl;

    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    if (att <= 0.0f)
        return 0.0f;

    lightStrength *= att;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    float3 dVec = L.Position - pos;
    float distSq = dot(dVec, dVec);
    float rangeSq = L.FalloffEnd * L.FalloffEnd;

    if (distSq > rangeSq)
        return 0.0f;

    float d = sqrt(distSq);
    float3 lightVec = dVec / d;

    float ndotl = max(dot(lightVec, normal), 0.0f);
    if (ndotl <= 0.0f)
        return 0.0f;

    float3 lightStrength = L.Strength * ndotl;

    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    if (att <= 0.0f)
        return 0.0f;

    lightStrength *= att;

    float spotFactor = dot(-lightVec, L.Direction);

    if (spotFactor <= 0.0f)
        return 0.0f;

    spotFactor = pow(spotFactor, L.SpotPower);

    if (spotFactor <= 0.001f)
        return 0.0f;

    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}


