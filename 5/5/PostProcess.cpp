#include "PostProcess.h"
#include "ThrowIfFaild.h"

PostProcess::PostProcess(ID3D12Device* device, int width, int height, CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle, UINT srvDescriptorSize) 
            : _srvHandle(srvHandle), _srvDescriptorSize(srvDescriptorSize)
{
    CreateResources(device, width, height);

    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = 1;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&RTVHeap)));

    CreateRTV(device);
    CreateSRV(device);
}

void PostProcess::CreateResources(ID3D12Device* device, int width, int height)
{
    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = HDR_FORMAT;
    desc.SampleDesc = { 1, 0 };
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = HDR_FORMAT;
    clear.Color[0] = clear.Color[1] = clear.Color[2] = 0.0f;
    clear.Color[3] = 1.0f;

    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(device->CreateCommittedResource(
        &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        &clear, IID_PPV_ARGS(&HDRBuffer)));
}

void PostProcess::CreateRTV(ID3D12Device* device)
{
    HDR_RTV = RTVHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = HDR_FORMAT;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    device->CreateRenderTargetView(HDRBuffer.Get(), &rtvDesc, HDR_RTV);
}

void PostProcess::CreateSRV(ID3D12Device* device)
{
    HDR_SRV = _srvHandle;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = HDR_FORMAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(HDRBuffer.Get(), &srvDesc, HDR_SRV);
}

void PostProcess::TransitToRenderTarget(ID3D12GraphicsCommandList* cmdList)
{
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        HDRBuffer.Get(),
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &barrier);
}

void PostProcess::TransitToShaderResource(ID3D12GraphicsCommandList* cmdList)
{
    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
        HDRBuffer.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &barrier);
}

void PostProcess::OnResize(ID3D12Device* device, int width, int height)
{
    HDRBuffer.Reset();

    CreateResources(device, width, height);
    CreateRTV(device);
    CreateSRV(device);
}