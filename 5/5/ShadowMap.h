#ifndef SHADOW_MAP_HPP
#define SHADOW_MAP_HPP

#include <Windows.h>
#include <wrl/client.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <dxgiformat.h>
#include <DirectXMath.h>

using namespace Microsoft::WRL;
using namespace DirectX;

constexpr int SHADOW_MAP_TEXTURE_DEFAULT_SIZE = 1024;
constexpr int CASCADES_COUNT = 3;

class ShadowMap
{
public:
    ShadowMap(ID3D12Device* device, CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle, UINT srvDescriptorSize);

    void TransitToOpaqueRenderingState(ID3D12GraphicsCommandList* cmdList);
    void TransitToLightRenderingState(ID3D12GraphicsCommandList* cmdList);

    void ClearView(ID3D12GraphicsCommandList* cmdList);

    ComPtr<ID3D12Resource> Resource;
    D3D12_CPU_DESCRIPTOR_HANDLE SRV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE DSVs[CASCADES_COUNT] = {};

    ComPtr<ID3D12DescriptorHeap> DSVHeap;

private:
    void CreateTexture(ID3D12Device* device);
    void CreateSRVAndDSV(ID3D12Device* device);

    CD3DX12_CPU_DESCRIPTOR_HANDLE _srvHandle;
    UINT _srvDescriptorSize = 0;
};

#endif // !SHADOW_MAP_HPP