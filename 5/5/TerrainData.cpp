#include "TerrainData.h"
#include <d3dx12.h>
#include <WICTextureLoader.h>
#include <ResourceUploadBatch.h>
#include "ThrowIfFaild.h"
#include "PathManager.h"

TerrainData::TerrainData(GraphicsDevice& device, const TerrainDesc& desc) : _desc(desc)
{
	_srv = device.Srv().Allocate(3);

	LoadTextures(device.Device(), device.Queue());

	CreateLayerArray(device.Device(), device.CommandList());
	CreateMaskTexture(device.Device(), device.Queue());
	CreateSRV(device.Device());

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_heightMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	device.CommandList()->ResourceBarrier(1, &barrier);
}

ComPtr<ID3D12Resource> TerrainData::LoadWic(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const std::wstring& path, bool isColor)
{
	ComPtr<ID3D12Resource> res;

	DirectX::ResourceUploadBatch resourceUpload(device);
	resourceUpload.Begin();

	ThrowIfFailed(CreateWICTextureFromFileEx(device, resourceUpload, path.c_str(), 0, D3D12_RESOURCE_FLAG_NONE, isColor ? (DirectX::WIC_LOADER_FORCE_SRGB | DirectX::WIC_LOADER_MIP_AUTOGEN | DirectX::WIC_LOADER_FORCE_RGBA32) : DirectX::WIC_LOADER_DEFAULT, res.ReleaseAndGetAddressOf()));

	resourceUpload.End(cmdQueue).wait();

	return res;
}

void TerrainData::LoadTextures(ID3D12Device* device, ID3D12CommandQueue* cmdQueue)
{
	const std::wstring folder = LOCAL_PATH_W + TERRAIN_FOLDER_W;

	_heightMap = LoadWic(device, cmdQueue, folder + _desc.HeightMap, false);

	const D3D12_RESOURCE_DESC heightDesc = _heightMap->GetDesc();
	assert(heightDesc.Format == DXGI_FORMAT_R16_UNORM);

	for (const auto& info : _desc.Layers)
	{
		_layerSources.push_back(LoadWic(device, cmdQueue, folder + info.File, true));
	}

	assert(LayerCount() != 0 && LayerCount() <= TERRAIN_MAX_LAYERS);
}

void TerrainData::CreateLayerArray(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
	const D3D12_RESOURCE_DESC first = _layerSources[0]->GetDesc();

	for (const auto& tex : _layerSources)
	{
		const D3D12_RESOURCE_DESC d = tex->GetDesc();
		assert(d.Width == first.Width && d.Height == first.Height);
		assert(d.Format == first.Format && d.MipLevels == first.MipLevels);
	}

	D3D12_RESOURCE_DESC arrayDesc = CD3DX12_RESOURCE_DESC::Tex2D(first.Format, first.Width, first.Height, (UINT16)LayerCount(), first.MipLevels);
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &arrayDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(_layerArray.GetAddressOf())));

	std::vector<CD3DX12_RESOURCE_BARRIER> toSource;
	for (const auto& tex : _layerSources)
	{
		toSource.push_back(CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE));
	}
	cmdList->ResourceBarrier((UINT)toSource.size(), toSource.data());

	for (UINT layer = 0; layer < LayerCount(); layer++)
	{
		for (UINT mip = 0; mip < first.MipLevels; mip++)
		{
			CD3DX12_TEXTURE_COPY_LOCATION src(_layerSources[layer].Get(), mip);
			CD3DX12_TEXTURE_COPY_LOCATION dst(_layerArray.Get(), mip + layer * first.MipLevels);
			cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}
	}

	std::vector<CD3DX12_RESOURCE_BARRIER> back;
	back.push_back(CD3DX12_RESOURCE_BARRIER::Transition(_layerArray.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	for (const auto& tex : _layerSources)
	{
		back.push_back(CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
	}
	cmdList->ResourceBarrier((UINT)back.size(), back.data());
}

void TerrainData::CreateMaskTexture(ID3D12Device* device, ID3D12CommandQueue* cmdQueue)
{
	_maskCount = (LayerCount() + 3) / 4;

	UINT s = _desc.MaskSize;
	UINT ss = _desc.MaskSize * _desc.MaskSize;

	std::vector<uint8_t> pixels(ss * 4 * _maskCount, 0);
	for (size_t i = 0; i < (size_t)ss; i++) { pixels[i * 4 + 0] = 255; }

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, s, s, (UINT16)_maskCount, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(_maskArray.GetAddressOf())));

	const size_t sliceBytes = (size_t)ss * 4;
	const LONG_PTR rowPitch = (LONG_PTR)s * 4;
	const LONG_PTR slicePitch = (LONG_PTR)sliceBytes;

	std::vector<D3D12_SUBRESOURCE_DATA> data(_maskCount);
	for (UINT i = 0; i < _maskCount; i++)
	{
		data[i].pData = pixels.data() + i * sliceBytes;
		data[i].RowPitch = rowPitch;
		data[i].SlicePitch = slicePitch;
	}

	DirectX::ResourceUploadBatch upload(device);
	upload.Begin();
	upload.Upload(_maskArray.Get(), 0, data.data(), _maskCount);
	upload.Transition(_maskArray.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	upload.End(cmdQueue).wait();
}

void TerrainData::CreateSRV(ID3D12Device* device)
{
	{
		const D3D12_RESOURCE_DESC desc = _heightMap.Get()->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = desc.MipLevels;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(_heightMap.Get(), &srvDesc, _srv.Cpu(0));
	}

	{
		const D3D12_RESOURCE_DESC desc = _layerArray.Get()->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.ArraySize = LayerCount();
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.MipLevels = desc.MipLevels;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(_layerArray.Get(), &srvDesc, _srv.Cpu(1));
	}

	{
		const D3D12_RESOURCE_DESC desc = _maskArray.Get()->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.ArraySize = _maskCount;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.MipLevels = 1;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(_maskArray.Get(), &srvDesc, _srv.Cpu(2));
	}
}