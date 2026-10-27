#include "VelocityBuffers.h"
#include "ThrowIfFaild.h"

VelocityBuffers::VelocityBuffers(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT firstSlot, UINT srvDescSize) : 
    _srvHeap(srvHeap), _firstSlot(firstSlot), _srvDescSize(srvDescSize), _rtvDescSize(device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV))
{
    D3D12_DESCRIPTOR_HEAP_DESC hd = {};
    hd.NumDescriptors = 2;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&_rtvHeap)));
}

void VelocityBuffers::OnResize(ID3D12Device* d, UINT w, UINT h)
{
    _current = 1;

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16_FLOAT, w, h, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_R16G16_FLOAT;
    clear.Color[1] = 0.0f;

    for (int i = 0; i < 2; i++)
    {
        ThrowIfFailed(d->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(_buffer[i].ReleaseAndGetAddressOf())));
    }

    CreateViews(d);
}

void VelocityBuffers::CreateViews(ID3D12Device* device)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(_srvHeap->GetCPUDescriptorHandleForHeapStart(), _firstSlot, _srvDescSize);

    const D3D12_RESOURCE_DESC velocityDesc = _buffer[0].Get()->GetDesc();

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

            device->CreateShaderResourceView(_buffer[i].Get(), &srvDesc, srv);
            srv.Offset(1, _srvDescSize);
        }
    }

    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvStart(_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_RENDER_TARGET_VIEW_DESC velRTVDesc = {};
        velRTVDesc.Format = velocityDesc.Format;
        velRTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        for (int i = 0; i < 2; i++)
        {
            _rtv[i] = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, i, _rtvDescSize);
            device->CreateRenderTargetView(_buffer[i].Get(), &velRTVDesc, _rtv[i]);
        }
    }
}

void VelocityBuffers::ClearCurrent(ID3D12GraphicsCommandList* cmdList)
{
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };

	cmdList->ClearRenderTargetView(_rtv[_current], clearColor, 0, nullptr);
}

void VelocityBuffers::TransitCurrentToWrite(ID3D12GraphicsCommandList* cmdList)
{
	auto b = CD3DX12_RESOURCE_BARRIER::Transition(_buffer[_current].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmdList->ResourceBarrier(1, &b);
}

void VelocityBuffers::TransitCurrentToRead(ID3D12GraphicsCommandList* cmdList)
{
	auto b = CD3DX12_RESOURCE_BARRIER::Transition(_buffer[_current].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	cmdList->ResourceBarrier(1, &b);
}