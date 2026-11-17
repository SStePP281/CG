#ifndef TERRAIN_DATA_HPP
#define TERRAIN_DATA_HPP

#include <string>
#include <vector>
#include <wrl/client.h>
#include <d3d12.h>
#include <DirectXCollision.h>
#include "GraphicsDevice.h"
#include "SlotHandle.h"

using Microsoft::WRL::ComPtr;

constexpr UINT TERRAIN_MAX_LAYERS = 16;

struct TerrainLayer
{
	std::wstring File;
	float TileSize;
};

struct TerrainDesc
{
	std::wstring HeightMap;
	std::vector<TerrainLayer> Layers;

	float WorldSize = 1024.0f;
	float HeightScale = 200.0f;
	float HeightOffset = -100.0f;
	UINT MaskSize = 2048;
};

class TerrainData
{
public:
	TerrainData(GraphicsDevice& device, const TerrainDesc& desc);

	TerrainData(const TerrainData&) = delete;
	TerrainData& operator=(const TerrainData&) = delete;

	float WorldSize() const { return _desc.WorldSize; }
	float WorldOffset() const { return -_desc.WorldSize * 0.5f; }
	float HeightScale() const { return _desc.HeightScale; }
	float HeightOffset() const { return _desc.HeightOffset; }
	UINT MaskSize() const { return _desc.MaskSize; }

	UINT LayerCount() const { return (UINT)_desc.Layers.size(); }
	UINT MaskCount() const { return _maskCount; }
	const std::vector<TerrainLayer>& Layers() const { return _desc.Layers; }

	ID3D12Resource* HeightMap() const { return _heightMap.Get(); }
	ID3D12Resource* MaskArray() const { return _maskArray.Get(); }

	DescriptorRange Srv() const { return _srv; }

	DirectX::BoundingBox Bounds() const { return DirectX::BoundingBox({ 0.0f, HeightOffset() + HeightScale() * 0.5f, 0.0f }, { WorldSize() * 0.5f, HeightScale() * 0.5f, WorldSize() * 0.5f }); }

private:
	static ComPtr<ID3D12Resource> LoadWic(ID3D12Device* device, ID3D12CommandQueue* queue, const std::wstring& path, bool isColor);

	void LoadTextures(ID3D12Device* device, ID3D12CommandQueue* queue);
	void CreateLayerArray(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
	void CreateMaskTexture(ID3D12Device* device, ID3D12CommandQueue* queue);
	void CreateSRV(ID3D12Device* device);

private:
	TerrainDesc _desc;

	ComPtr<ID3D12Resource> _heightMap;

	ComPtr<ID3D12Resource> _layerArray;
	std::vector<ComPtr<ID3D12Resource>> _layerSources;

	ComPtr<ID3D12Resource> _maskArray;
	UINT _maskCount = 0;

	DescriptorRange _srv;
};
using TerrainHandle = SlotHandle<TerrainData>;

#endif