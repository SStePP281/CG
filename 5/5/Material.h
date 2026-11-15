#ifndef MATERIAL_HPP
#define MATERIAL_HPP

#include <string>
#include <DirectXMath.h>
#include "SlotHandle.h"
#include "Texture.h"

struct Material;
using MaterialHandle = SlotHandle<Material>;

struct Material
{
    std::string Name;

    TextureHandle AlbedoMap;
    TextureHandle NormalMap;
    TextureHandle MetallicMap;
    TextureHandle RoughnessMap;
    TextureHandle AOMap;
    TextureHandle DisplacementMap;

    DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    float Roughness = 0.5f;
    float Metallic = 0.0f;
    float NormalIntensity = 1.0f;

    int NumFramesDirty = 3;
};

#endif // !MATERIAL_HPP
