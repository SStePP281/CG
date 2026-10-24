#include "TAA.h"
#include "ThrowIfFaild.h"

TAA::TAA(ID3D12Device* device, CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle, UINT srvDescriptorSize, UINT width, UINT height)
    : _srvHandle(srvHandle), _srvDescriptorSize(srvDescriptorSize), _width(width), _height(height)
{
    _rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 4;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_taaRTVHeap)));

    CreateResources(device);
    CreateSRVAndRTV(device);
}

void TAA::OnResize(ID3D12Device* device, UINT width, UINT height)
{
    _historyIndex = 0;
    _frameCount = 0;

    _width = width;
    _height = height;

    CreateResources(device);
    CreateSRVAndRTV(device);
}

void TAA::TransitCurrentToWrite(ID3D12GraphicsCommandList* cmdList)
{
    auto b = CD3DX12_RESOURCE_BARRIER::Transition(_colorBuffer[1 - _historyIndex].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &b);
}

void TAA::TransitCurrentToRead(ID3D12GraphicsCommandList* cmdList)
{
    auto b = CD3DX12_RESOURCE_BARRIER::Transition(_colorBuffer[1 - _historyIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &b);
}

void TAA::CreateResources(ID3D12Device* device)
{
    {
        _colorBuffer.resize(2);

        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, _width, _height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);

        D3D12_CLEAR_VALUE clear = {};
        clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        clear.Color[3] = 0.0f;

        for (int i = 0; i < 2; i++)
        {
            ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(_colorBuffer[i].ReleaseAndGetAddressOf())));
        }
    }

    {
        _velocityBuffer.resize(2);

        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, _width, _height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
        CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);

        D3D12_CLEAR_VALUE clear = {};
        clear.Format = DXGI_FORMAT_R16G16_FLOAT;
        clear.Color[1] = 0.0f;

        for (int i = 0; i < 2; i++)
        {
            ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(_velocityBuffer[i].ReleaseAndGetAddressOf())));
        }
    }
}

void TAA::CreateSRVAndRTV(ID3D12Device* device)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE srv = _srvHandle;

    const D3D12_RESOURCE_DESC colorDesc = _colorBuffer[0].Get()->GetDesc();
    const D3D12_RESOURCE_DESC velocityDesc = _velocityBuffer[0].Get()->GetDesc();

    {
        for (int i = 0; i < 2; i++)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = colorDesc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = colorDesc.MipLevels;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(_colorBuffer[i].Get(), &srvDesc, srv);
            srv.Offset(1, _srvDescriptorSize);
        }
    }

    {
        for (int i = 0; i < 2; i++)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = velocityDesc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = velocityDesc.MipLevels;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(_velocityBuffer[i].Get(), &srvDesc, srv);
            srv.Offset(1, _srvDescriptorSize);
        }
    }

    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvStart(_taaRTVHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_RENDER_TARGET_VIEW_DESC colorRTVDesc = {};
        colorRTVDesc.Format = colorDesc.Format;
        colorRTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        D3D12_RENDER_TARGET_VIEW_DESC velRTVDesc = {};
        velRTVDesc.Format = velocityDesc.Format;
        velRTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        for (int i = 0; i < 2; i++)
        {
            _colorRTVs[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, i, _rtvDescriptorSize);
            device->CreateRenderTargetView(_colorBuffer[i].Get(), &colorRTVDesc, _colorRTVs[i]);

            _velocityRTVs[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, 2 + i, _rtvDescriptorSize);
            device->CreateRenderTargetView(_velocityBuffer[i].Get(), &velRTVDesc, _velocityRTVs[i]);
        }
    }
}

XMFLOAT4X4 TAA::GetJitteredMatrix(XMFLOAT4X4 cameraViewProj)
{
    XMFLOAT2 jitter = Generate();
    _frameCount = (_frameCount + 1) % JITTER_COUNT;

    XMStoreFloat4x4(&cameraViewProj, XMMatrixMultiply(XMLoadFloat4x4(&cameraViewProj), XMMatrixTranslation(jitter.x, jitter.y, 0)));

    return cameraViewProj;
}

float TAA::Halton(UINT index, UINT base)
{
    float f = 1.0f;
    float result = 0.0f;

    while (index > 0)
    {
        f /= (float)base;
        result += f * (float)(index % base);
        index = (UINT)(floorf((float)index / base));
    }

    return result;
}
