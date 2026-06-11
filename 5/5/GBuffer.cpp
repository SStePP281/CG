#include "GBuffer.h"
#include "ThrowIfFaild.h"
#include <d3dx12.h>

GBuffer::GBuffer(ID3D12Device* device, int width, int height, CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle, UINT srvDescriptorSize)
    : _srvHandle(srvHandle), _srvDescriptorSize(srvDescriptorSize)
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
    rtvDesc.NumDescriptors = 3;
    rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&RTVHeap)));

    D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
    dsvDesc.NumDescriptors = 1;
    dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&DSVHeap)));

    Textures.resize((UINT)GBufferIndex::Count);

    CreateTextures(device, width, height);
    CreateRTVandDSV(device);
    CreateSRV(device);
}

void GBuffer::CreateTextures(ID3D12Device* device, int width, int height)
{
    CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, width, height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        if (i == (UINT)GBufferIndex::Depth)
        {
            resDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
            resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
            clearValue.DepthStencil.Depth = 1.0f;
            clearValue.DepthStencil.Stencil = 0;
        }
        else
        {
            resDesc.Format = INFO_FORMATS[i];
            resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

            clearValue.Format = INFO_FORMATS[i];
            clearValue.Color[0] = 0.0f;
            clearValue.Color[1] = 0.0f;
            clearValue.Color[2] = 0.0f;
            clearValue.Color[3] = 1.0f;
        }

        ThrowIfFailed(device->CreateCommittedResource(
            &heapProp,
            D3D12_HEAP_FLAG_NONE,
            &resDesc,
            D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
            &clearValue,
            IID_PPV_ARGS(&Textures[i].Resource)));
    }
}

void GBuffer::CreateSRV(ID3D12Device* device)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE hSrv = _srvHandle;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        srvDesc.Format = INFO_FORMATS[i];
        Textures[i].SRV = hSrv;

        device->CreateShaderResourceView(Textures[i].Resource.Get(), &srvDesc, hSrv);
        hSrv.Offset(1, _srvDescriptorSize);
    }
}

void GBuffer::CreateRTVandDSV(ID3D12Device* device)
{
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    UINT rtvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE hRtv(RTVHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    UINT dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDsv(DSVHeap->GetCPUDescriptorHandleForHeapStart());

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        if (i == (UINT)GBufferIndex::Depth)
        {
            Textures[i].DSV = hDsv;
            device->CreateDepthStencilView(Textures[i].Resource.Get(), &dsvDesc, hDsv);
            hDsv.Offset(1, dsvSize);
        }
        else
        {
            rtvDesc.Format = INFO_FORMATS[i];
            Textures[i].RTV = hRtv;
            device->CreateRenderTargetView(Textures[i].Resource.Get(), &rtvDesc, hRtv);
            hRtv.Offset(1, rtvSize);
        }
    }
}

void GBuffer::TransitToOpaqueRenderingState(ID3D12GraphicsCommandList* cmdList)
{
    std::vector<D3D12_RESOURCE_BARRIER> barriers;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        CD3DX12_RESOURCE_BARRIER barrier;

        if (i == (UINT)GBufferIndex::Depth)
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                Textures[i].Resource.Get(),
                D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_DEPTH_WRITE);
        else
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                Textures[i].Resource.Get(),
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE,
                D3D12_RESOURCE_STATE_RENDER_TARGET);

        barriers.push_back(barrier);
    }

    cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

void GBuffer::TransitToLightRenderingState(ID3D12GraphicsCommandList* cmdList)
{
    std::vector<D3D12_RESOURCE_BARRIER> barriers;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        CD3DX12_RESOURCE_BARRIER barrier;

        if (i == (UINT)GBufferIndex::Depth)
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                Textures[i].Resource.Get(),
                D3D12_RESOURCE_STATE_DEPTH_WRITE,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_DEPTH_READ);
        else
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                Textures[i].Resource.Get(),
                D3D12_RESOURCE_STATE_RENDER_TARGET,
                D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);

        barriers.push_back(barrier);
    }

    cmdList->ResourceBarrier((UINT)barriers.size(), barriers.data());
}

void GBuffer::ClearView(ID3D12GraphicsCommandList* cmdList)
{
    const float clearColor[] = { 0.0f, 0.0f, 0.0f, 1.0f };

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        if (i == (UINT)GBufferIndex::Depth)
            cmdList->ClearDepthStencilView(
                Textures[i].DSV,
                D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
                1.0f, 0, 0, nullptr);
        else
            cmdList->ClearRenderTargetView(Textures[i].RTV, clearColor, 0, nullptr);
    }
}

void GBuffer::OnResize(ID3D12Device* device, int width, int height)
{
    for (auto& t : Textures) t.Resource.Reset();

    CreateTextures(device, width, height);
    CreateRTVandDSV(device);
    CreateSRV(device);
}