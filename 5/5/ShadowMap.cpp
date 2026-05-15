#include "ShadowMap.h"
#include "ThrowIfFaild.h"
#include <d3dx12.h>

ShadowMap::ShadowMap(ID3D12Device* device, CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle, UINT srvDescriptorSize) : _srvHandle(srvHandle), _srvDescriptorSize(srvDescriptorSize)
{
	D3D12_DESCRIPTOR_HEAP_DESC dsvDesc = {};
	dsvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvDesc.NumDescriptors = CASCADES_COUNT;
	dsvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	ThrowIfFailed(device->CreateDescriptorHeap(&dsvDesc, IID_PPV_ARGS(&DSVHeap)));

	CreateTexture(device);
	CreateSRVAndDSV(device);
}

void ShadowMap::CreateTexture(ID3D12Device* device)
{
	CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R24G8_TYPELESS,
		SHADOW_MAP_TEXTURE_DEFAULT_SIZE,
		SHADOW_MAP_TEXTURE_DEFAULT_SIZE,
		CASCADES_COUNT,
		1, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL
	);

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	ThrowIfFailed(device->CreateCommittedResource(
		&heapProp,
		D3D12_HEAP_FLAG_NONE,
		&resDesc,
		D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&clearValue,
		IID_PPV_ARGS(&Resource)));
}

void ShadowMap::CreateSRVAndDSV(ID3D12Device* device)
{
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Texture2DArray.MipSlice = 0;
	dsvDesc.Texture2DArray.ArraySize = 1;

	UINT dsvSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDsv(DSVHeap->GetCPUDescriptorHandleForHeapStart());

	for (int i = 0; i < CASCADES_COUNT; i++)
	{
		dsvDesc.Texture2DArray.FirstArraySlice = i;
		device->CreateDepthStencilView(Resource.Get(), &dsvDesc, hDsv);
		DSVs[i] = hDsv;
		hDsv.Offset(1, dsvSize);
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = CASCADES_COUNT;

	device->CreateShaderResourceView(Resource.Get(), &srvDesc, _srvHandle);
	SRV = _srvHandle;
}

void ShadowMap::TransitToOpaqueRenderingState(ID3D12GraphicsCommandList* cmdList)
{
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		Resource.Get(),
		D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		D3D12_RESOURCE_STATE_DEPTH_WRITE
	);

	cmdList->ResourceBarrier(1, &barrier);
}

void ShadowMap::TransitToLightRenderingState(ID3D12GraphicsCommandList* cmdList)
{
	CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		Resource.Get(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
	);

	cmdList->ResourceBarrier(1, &barrier);
}

void ShadowMap::ClearView(ID3D12GraphicsCommandList* cmdList)
{
	for (int i = 0; i < CASCADES_COUNT; i++)
	{
		cmdList->ClearDepthStencilView(DSVs[i], D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	}
}
