#ifndef TEXTURE_MANAGER_HPP
#define TEXTURE_MANAGER_HPP

#include <d3d12.h>
#include <wrl/client.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

#include "DescriptorHeapAllocator.h"
#include "ModelStruct.h"
#include "DeferredReleaseQueue.h"

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
    void Initialize(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, ID3D12GraphicsCommandList* cmdList, DescriptorAllocator* srvAlloc, DeferredReleaseQueue* releaseQueue, UINT64 uploadFence);

    TextureHandle Load(const std::string& name, const std::wstring& path);
    TextureHandle Find(const std::string& name) const;

    void Unload(TextureHandle h, UINT64 fenceValue);

    TextureHandle Default(DefaultTex t) const { return _defaults[(size_t)t]; }

    D3D12_GPU_DESCRIPTOR_HANDLE Srv(TextureHandle h, DefaultTex fallback) const;

private:
    TextureHandle Register(std::unique_ptr<Texture> tex);
    const Texture* Get(TextureHandle h) const;

    void CreateSrv(Texture& tex);
    void CreateDefaultTextures(UINT64 uploadFence);
    ComPtr<ID3D12Resource> CreateDefault1x1Texture(DXGI_FORMAT format, const UINT8 rgba[4], ComPtr<ID3D12Resource>& uploadHeap);

private:
    ID3D12Device* _device = nullptr;
    ID3D12CommandQueue* _cmdQueue = nullptr;
    ID3D12GraphicsCommandList* _cmdList = nullptr;
    DescriptorAllocator* _srvAlloc = nullptr;
    DeferredReleaseQueue* _releaseQueue = nullptr;

    struct Slot
    {
        std::unique_ptr<Texture> Tex;
        uint32_t Generation = 0;
    };

    std::vector<Slot> _slots;
    std::vector<uint32_t> _freeSlots;
    std::unordered_map<std::string, TextureHandle> _byName;

    TextureHandle _defaults[(size_t)DefaultTex::Count];
};

#endif // !TEXTURE_MANAGER_HPP