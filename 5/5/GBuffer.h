#ifndef G_BUFFER_HPP
#define G_BUFFER_HPP

#include <string>
#include <vector>

#include <Windows.h>
#include <wrl/client.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <dxgiformat.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include "RenderSettings.h"
#include "DescriptorHeapAllocator.h"

using namespace Microsoft::WRL;
using namespace DirectX;

enum class GBufferIndex : UINT
{
    Albedo = 0,
    Normal = 1,
    MatData = 2,   //metallic, roughness, AO
    Depth = 3,
    Count = 4
};

struct GBufferTexture
{
    ComPtr<ID3D12Resource> Resource = nullptr;

    DescriptorRange RTV = {};
    DescriptorRange SRV = {};
    DescriptorRange DSV = {};
};

class GBuffer
{
public:
    static constexpr DXGI_FORMAT DEPTH_RESOURCE_FORMAT = DXGI_FORMAT_R24G8_TYPELESS;
    static constexpr DXGI_FORMAT DEPTH_DSV_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
    static constexpr DXGI_FORMAT DEPTH_SRV_FORMAT = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

    static constexpr DXGI_FORMAT INFO_FORMATS[(UINT)GBufferIndex::Count] =
    {
        DXGI_FORMAT_R8G8B8A8_UNORM,         // Albedo
        DXGI_FORMAT_R32G32B32A32_FLOAT,     // Normal
        DXGI_FORMAT_R8G8B8A8_UNORM,         // MatData (metallic, roughness, AO)
        DEPTH_SRV_FORMAT                    // Depth (SRV формат)
    };

public:
    GBuffer(ID3D12Device* device, Resolution res, DescriptorAllocator* srvAlloc, DescriptorAllocator* rtvAlloc, DescriptorAllocator* dsvAlloc);

    void TransitToOpaqueRenderingState(ID3D12GraphicsCommandList* cmdList);
    void TransitToLightRenderingState(ID3D12GraphicsCommandList* cmdList);

    void ClearView(ID3D12GraphicsCommandList* cmdList);

    void OnResize(ID3D12Device* device, Resolution res);

    std::vector<GBufferTexture> Textures;

private:
    void CreateTextures(ID3D12Device* device, Resolution res);
    void CreateSRV(ID3D12Device* device);
    void CreateRTVandDSV(ID3D12Device* device);

    DescriptorRange _srvRange;
    DescriptorRange _rtvRange;
    DescriptorRange _dsvRange;
};

#endif // !G_BUFFER_HPP