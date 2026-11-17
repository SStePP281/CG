#include "GBuffer.h"
#include "ThrowIfFaild.h"
#include <d3dx12.h>

GBuffer::GBuffer(GraphicsDevice& device, Resolution res)
{
    _srvRange = device.Srv().Allocate((UINT)GBufferIndex::Count + 1); // +1: stencil plane
    _rtvRange = device.Rtv().Allocate((UINT)GBufferIndex::Count - 1);
    _dsvRange = device.Dsv().Allocate(1);

    Textures.resize((UINT)GBufferIndex::Count);

    CreateTextures(device.Device(), res);
    CreateRTVandDSV(device.Device());
    CreateSRV(device.Device());
}

void GBuffer::CreateTextures(ID3D12Device* device, Resolution res)
{
    CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, res.Width, res.Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

        if (i == (UINT)GBufferIndex::Depth)
        {
            resDesc.Format = DEPTH_RESOURCE_FORMAT;
            resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            clearValue.Format = DEPTH_DSV_FORMAT;
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

        D3D12_RESOURCE_STATES initialState = (i == (UINT)GBufferIndex::Depth) ? (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) : D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE;

        ThrowIfFailed(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, initialState, &clearValue, IID_PPV_ARGS(&Textures[i].Resource)));
    }
}

void GBuffer::CreateSRV(ID3D12Device* device)
{
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        srvDesc.Format = INFO_FORMATS[i];
        Textures[i].SRV = _srvRange.Sub(i);

        device->CreateShaderResourceView(Textures[i].Resource.Get(), &srvDesc, _srvRange.Cpu(i));
    }

    Textures[(UINT)GBufferIndex::Depth].SRV = _srvRange.Sub((UINT)GBufferIndex::Depth, 2);

    D3D12_SHADER_RESOURCE_VIEW_DESC stencilDesc = {};
    stencilDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    stencilDesc.Format = DXGI_FORMAT_X24_TYPELESS_G8_UINT;
    stencilDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    stencilDesc.Texture2D.MipLevels = 1;
    stencilDesc.Texture2D.PlaneSlice = 1;
    device->CreateShaderResourceView(Textures[(UINT)GBufferIndex::Depth].Resource.Get(), &stencilDesc, _srvRange.Cpu((UINT)GBufferIndex::Count));
}

void GBuffer::CreateRTVandDSV(ID3D12Device* device)
{
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DEPTH_DSV_FORMAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Texture2D.MipSlice = 0;

    for (UINT i = 0; i < (UINT)GBufferIndex::Count; ++i)
    {
        if (i == (UINT)GBufferIndex::Depth)
        {
            Textures[i].DSV = _dsvRange;
            device->CreateDepthStencilView(Textures[i].Resource.Get(), &dsvDesc, _dsvRange.Cpu());
        }
        else
        {
            rtvDesc.Format = INFO_FORMATS[i];
            Textures[i].RTV = _rtvRange.Sub(i);
            device->CreateRenderTargetView(Textures[i].Resource.Get(), &rtvDesc, _rtvRange.Cpu(i));
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
        {
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(Textures[i].Resource.Get(), D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }
        else
        {
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(Textures[i].Resource.Get(), D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        }

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
        {
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(Textures[i].Resource.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_DEPTH_READ);
        }
        else
        {
            barrier = CD3DX12_RESOURCE_BARRIER::Transition(Textures[i].Resource.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_ALL_SHADER_RESOURCE);
        }

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
        {
            cmdList->ClearDepthStencilView(Textures[i].DSV.Cpu(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
        }
        else
        {
            cmdList->ClearRenderTargetView(Textures[i].RTV.Cpu(), clearColor, 0, nullptr);
        }
    }
}

void GBuffer::OnResize(ID3D12Device* device, Resolution res)
{
    for (auto& t : Textures) t.Resource.Reset();

    CreateTextures(device, res);
    CreateRTVandDSV(device);
    CreateSRV(device);
}