#ifndef TEXTURE_MANAGER_HPP
#define TEXTURE_MANAGER_HPP

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

#include "Texture.h"
#include "SlotPool.h"
#include "GraphicsDevice.h"

using Microsoft::WRL::ComPtr;

enum class DefaultTex
{
    Albedo,
    Normal,
    Metallic,
    Roughness,
    AO,
    Displacement,
    Count
};

class TextureManager
{
public:
    explicit TextureManager(GraphicsDevice& device);

    TextureHandle Load(const std::string& name, const std::wstring& path);
    TextureHandle Find(const std::string& name) const;
    void Unload(TextureHandle h);

    TextureHandle Default(DefaultTex t) const { return _defaults[(size_t)t]; }
    D3D12_GPU_DESCRIPTOR_HANDLE Srv(TextureHandle h, DefaultTex fallback) const;

private:
    TextureHandle Register(std::unique_ptr<Texture> tex);
    bool IsDefault(TextureHandle h) const;

    void CreateSrv(Texture& tex);
    void CreateDefaultTextures();
    ComPtr<ID3D12Resource> CreateDefault1x1Texture(DXGI_FORMAT format, const UINT8 rgba[4], ComPtr<ID3D12Resource>& uploadHeap);

private:
    GraphicsDevice& _device;

    SlotPool<Texture> _pool;
    std::unordered_map<std::string, TextureHandle> _byName;

    TextureHandle _defaults[(size_t)DefaultTex::Count];
};

#endif // !TEXTURE_MANAGER_HPP