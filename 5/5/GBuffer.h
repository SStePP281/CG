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

    D3D12_CPU_DESCRIPTOR_HANDLE RTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE SRV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE DSV = {};
};

class GBuffer
{
private:
    static constexpr DXGI_FORMAT INFO_FORMATS[(int)GBufferIndex::Count] =
    {
        DXGI_FORMAT_R8G8B8A8_UNORM,         // Albedo
        DXGI_FORMAT_R32G32B32A32_FLOAT,     // Normal
        DXGI_FORMAT_R8G8B8A8_UNORM,         // MatData (metallic, roughness, AO)
        DXGI_FORMAT_R24_UNORM_X8_TYPELESS   // Depth (SRV формат)
    };

public:
    GBuffer(ID3D12Device* device, int width, int height,
        CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle, UINT srvDescriptorSize);

    void TransitToOpaqueRenderingState(ID3D12GraphicsCommandList* cmdList);
    void TransitToLightRenderingState(ID3D12GraphicsCommandList* cmdList);

    void ClearView(ID3D12GraphicsCommandList* cmdList);

    void OnResize(ID3D12Device* device, int width, int height);

    std::vector<GBufferTexture> Textures;

    ComPtr<ID3D12DescriptorHeap> RTVHeap;
    ComPtr<ID3D12DescriptorHeap> DSVHeap;

private:
    void CreateTextures(ID3D12Device* device, int width, int height);
    void CreateSRV(ID3D12Device* device);
    void CreateRTVandDSV(ID3D12Device* device);

    CD3DX12_CPU_DESCRIPTOR_HANDLE _srvHandle;
    UINT _srvDescriptorSize = 0;
};

#endif // !G_BUFFER_HPP