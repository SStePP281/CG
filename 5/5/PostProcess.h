#ifndef POST_PROCESS_HPP
#define POST_PROCESS_HPP

#include <d3d12.h>
#include <d3dx12.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

constexpr DXGI_FORMAT HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

class PostProcess
{
public:
    PostProcess(ID3D12Device* device, int width, int height, CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle,UINT srvDescriptorSize);

    void OnResize(ID3D12Device* device, int width, int height);

    void TransitToRenderTarget(ID3D12GraphicsCommandList* cmdList);
    void TransitToShaderResource(ID3D12GraphicsCommandList* cmdList);

    ComPtr<ID3D12Resource>       HDRBuffer;
    D3D12_CPU_DESCRIPTOR_HANDLE  HDR_RTV = {};
    D3D12_CPU_DESCRIPTOR_HANDLE  HDR_SRV = {};

    ComPtr<ID3D12DescriptorHeap> RTVHeap;

private:
    void CreateResources(ID3D12Device* device, int width, int height);
    void CreateRTV(ID3D12Device* device);
    void CreateSRV(ID3D12Device* device);

    CD3DX12_CPU_DESCRIPTOR_HANDLE _srvHandle;
    UINT _srvDescriptorSize = 0;
};

#endif // !POST_PROCESS_HPP