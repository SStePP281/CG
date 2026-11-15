#include "TextureManager.h"
#include "ThrowIfFaild.h"
#include "WICTextureLoader.h"
#include <ResourceUploadBatch.h>
#include <d3dx12.h>
#include <cassert>

TextureManager::TextureManager(GraphicsDevice& device) : _device(device)
{
    CreateDefaultTextures();
}

TextureHandle TextureManager::Load(const std::string& name, const std::wstring& path)
{
    TextureHandle existing = Find(name);
    if (existing.Valid()) { return existing; }

    auto tex = std::make_unique<Texture>();
    tex->Name = name;
    tex->Filename = path;

    DirectX::ResourceUploadBatch upload(_device.Device());
    upload.Begin();
    ThrowIfFailed(DirectX::CreateWICTextureFromFile(_device.Device(), upload, path.c_str(), tex->Resource.ReleaseAndGetAddressOf(), true));
    upload.End(_device.Queue()).wait();

    tex->SrvRange = _device.Srv().Allocate(1);
    CreateSrv(*tex);

    return Register(std::move(tex));
}

void TextureManager::Unload(TextureHandle h)
{
    assert(!IsDefault(h) && "default textures cannot be unloaded");

    std::unique_ptr<Texture> tex = _pool.Remove(h);
    if (!tex) { return; }

    _byName.erase(tex->Name);
    _device.Release().Push(std::move(tex->Resource), &_device.Srv(), tex->SrvRange);
}

TextureHandle TextureManager::Find(const std::string& name) const
{
    auto it = _byName.find(name);
    return it != _byName.end() ? it->second : TextureHandle{};
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::Srv(TextureHandle h, DefaultTex fallback) const
{
    const Texture* tex = _pool.Get(h);
    if (!tex) { tex = _pool.Get(_defaults[(size_t)fallback]); }

    assert(tex);

    return tex->SrvRange.Gpu();
}

TextureHandle TextureManager::Register(std::unique_ptr<Texture> tex)
{
    std::string name = tex->Name;
    TextureHandle h = _pool.Add(std::move(tex));

    _byName[name] = h;

    return h;
}

void TextureManager::CreateSrv(Texture& tex)
{
    const D3D12_RESOURCE_DESC desc = tex.Resource->GetDesc();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = desc.MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    _device.Device()->CreateShaderResourceView(tex.Resource.Get(), &srvDesc, tex.SrvRange.Cpu());
}

bool TextureManager::IsDefault(TextureHandle h) const
{
    for (const TextureHandle& d : _defaults)
    {
        if (d == h) { return true; }
    }

    return false;
}

void TextureManager::CreateDefaultTextures()
{
    struct DefaultDesc { const char* name; UINT8 rgba[4]; };

    const DefaultDesc defaults[] =
    {
        { "T_DEFAULT_ALBEDO",       { 255, 255, 255, 255 } },
        { "T_DEFAULT_NORMAL",       { 128, 128, 255, 255 } },
        { "T_DEFAULT_METALLIC",  { 255, 255, 255, 255 } },
        { "T_DEFAULT_ROUGHNESS", { 255, 255, 255, 255 } },
        { "T_DEFAULT_AO",           { 255,   0,   0, 255 } },
        { "T_DEFAULT_DISPLACEMENT", { 128,   0,   0, 255 } },
    };

    static_assert(_countof(defaults) == (size_t)DefaultTex::Count, "defaults must match DefaultTex");

    for (size_t i = 0; i < (size_t)DefaultTex::Count; ++i)
    {
        auto tex = std::make_unique<Texture>();
        tex->Name = defaults[i].name;

        ComPtr<ID3D12Resource> upload;
        tex->Resource = CreateDefault1x1Texture(DXGI_FORMAT_R8G8B8A8_UNORM, defaults[i].rgba, upload);
        _device.Release().Push(upload);

        tex->SrvRange = _device.Srv().Allocate(1);
        CreateSrv(*tex);

        _defaults[i] = Register(std::move(tex));
    }
}

ComPtr<ID3D12Resource> TextureManager::CreateDefault1x1Texture(DXGI_FORMAT format, const UINT8 rgba[4], ComPtr<ID3D12Resource>& uploadHeap)
{
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = 1;
	texDesc.Height = 1;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = format;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	CD3DX12_HEAP_PROPERTIES defaultHeap(D3D12_HEAP_TYPE_DEFAULT);
	ComPtr<ID3D12Resource> tex;
	ThrowIfFailed(_device.Device()->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&tex)));

	UINT64 uploadSize = 0;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    _device.Device()->GetCopyableFootprints(&texDesc, 0, 1, 0, &footprint, nullptr, nullptr, &uploadSize);

	CD3DX12_HEAP_PROPERTIES uploadHeapProps(D3D12_HEAP_TYPE_UPLOAD);
	CD3DX12_RESOURCE_DESC uploadBufDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
	ThrowIfFailed(_device.Device()->CreateCommittedResource(&uploadHeapProps, D3D12_HEAP_FLAG_NONE, &uploadBufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&uploadHeap)));

	UINT8* mapped = nullptr;
	uploadHeap->Map(0, nullptr, reinterpret_cast<void**>(&mapped));

	mapped[0] = rgba[0];
	mapped[1] = rgba[1];
	mapped[2] = rgba[2];
	mapped[3] = rgba[3];
	uploadHeap->Unmap(0, nullptr);

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = tex.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = uploadHeap.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint = footprint;

    _device.CommandList()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    _device.CommandList()->ResourceBarrier(1, &barrier);

	return tex;
}