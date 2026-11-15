#include "ShadowMap.h"
#include "ThrowIfFaild.h"
#include <d3dx12.h>

ShadowMap::ShadowMap(GraphicsDevice& device, ShadowSettings& settings) : _settings(settings)
{
	SrvRange = device.Srv().Allocate(1);
	DsvRange = device.Dsv().Allocate(settings.CascadeCount);

	CreateTexture(device.Device());
	CreateSRVAndDSV(device.Device());
}

void ShadowMap::Render(ID3D12GraphicsCommandList* cmdList, const std::function<void(UINT cascade)>& drawCascade)
{
	const float size = (float)_settings.ShadowTextureSize;
	D3D12_VIEWPORT shadowViewport = { 0, 0, size, size, 0.0f, 1.0f };
	D3D12_RECT shadowScissor = { 0, 0, (LONG)size, (LONG)size };

	cmdList->RSSetViewports(1, &shadowViewport);
	cmdList->RSSetScissorRects(1, &shadowScissor);

	TransitToOpaqueRenderingState(cmdList);
	ClearView(cmdList);

	for (UINT k = 0; k < _settings.CascadeCount; k++)
	{
		auto dsv = DsvRange.Cpu(k);
		cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

		drawCascade(k);
	}

	TransitToLightRenderingState(cmdList);
}

void ShadowMap::CreateTexture(ID3D12Device* device)
{
	CD3DX12_HEAP_PROPERTIES heapProp(D3D12_HEAP_TYPE_DEFAULT);
	D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Tex2D(DEPTH_RESOURCE_FORMAT,_settings.ShadowTextureSize, _settings.ShadowTextureSize, _settings.CascadeCount, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = DEPTH_DSV_FORMAT;
	clearValue.DepthStencil.Depth = 1.0f;
	clearValue.DepthStencil.Stencil = 0;

	ThrowIfFailed(device->CreateCommittedResource(&heapProp, D3D12_HEAP_FLAG_NONE, &resDesc, D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clearValue, IID_PPV_ARGS(&Resource)));
}

void ShadowMap::CreateSRVAndDSV(ID3D12Device* device)
{
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
	dsvDesc.Format = DEPTH_DSV_FORMAT;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
	dsvDesc.Texture2DArray.MipSlice = 0;
	dsvDesc.Texture2DArray.ArraySize = 1;

	for (UINT i = 0; i < _settings.CascadeCount; i++)
	{
		dsvDesc.Texture2DArray.FirstArraySlice = i;
		device->CreateDepthStencilView(Resource.Get(), &dsvDesc, DsvRange.Cpu(i));
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DEPTH_SRV_FORMAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = 1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = _settings.CascadeCount;

	device->CreateShaderResourceView(Resource.Get(), &srvDesc, SrvRange.Cpu());
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
	for (UINT i = 0; i < _settings.CascadeCount; i++)
	{
		cmdList->ClearDepthStencilView(DsvRange.Cpu(i), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	}
}
