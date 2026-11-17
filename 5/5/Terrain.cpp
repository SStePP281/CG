#include "Terrain.h"

#include <cassert>

#include "PathManager.h"
#include "ThrowIfFaild.h"
#include "WICTextureLoader.h"
#include "ResourceUploadBatch.h"
#include <algorithm>
#include <cmath>
#include "D3DUtil.h"

using namespace DirectX;

Terrain::Terrain(GraphicsDevice& device)
{
	_descRange = device.Srv().Allocate(3);

	LoadTextures(device.Device(), device.Queue());

	CreateLayerArray(device.Device(), device.CommandList());
	CreateMaskTexture(device.Device(), device.Queue());
	CreateSRVAndUAV(device.Device());

	BuildTileMesh(device.Device(), device.CommandList());

	_root = BuildNode({ 0,0 }, TERRAIN_WORLD_SIZE, 0);

	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_heightMap.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	device.CommandList()->ResourceBarrier(1, &barrier);
}

ComPtr<ID3D12Resource> Terrain::LoadWic(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const std::wstring& path, bool isColor)
{
	ComPtr<ID3D12Resource> res;

	ResourceUploadBatch resourceUpload(device);
	resourceUpload.Begin();

	ThrowIfFailed(CreateWICTextureFromFileEx(
		device, resourceUpload, path.c_str(),
		0, D3D12_RESOURCE_FLAG_NONE,
		isColor ? (WIC_LOADER_FORCE_SRGB | WIC_LOADER_MIP_AUTOGEN | WIC_LOADER_FORCE_RGBA32) : WIC_LOADER_DEFAULT,
		res.ReleaseAndGetAddressOf()));

	resourceUpload.End(cmdQueue).wait();

	return res;
}

void Terrain::LoadTextures(ID3D12Device* device, ID3D12CommandQueue* cmdQueue)
{
	const std::wstring folder = LOCAL_PATH_W + TERRAIN_FOLDER_W;

	_heightMap = LoadWic(device, cmdQueue, folder + L"Height.png", false);

	const D3D12_RESOURCE_DESC heightDesc = _heightMap->GetDesc();
	_width = (UINT)heightDesc.Width;
	_height = heightDesc.Height;

	assert(heightDesc.Format == DXGI_FORMAT_R16_UNORM);

	const std::vector<LayerInfo> layers =
	{
		{ L"Default.png", TERRAIN_WORLD_SIZE },
		{ L"Grass.png",   16.0f },
		{ L"Stone.png",   32.0f },
		{ L"Muddy.png",   32.0f }
	};

	for (const auto& info : layers)
	{
		_layerSources.push_back(LoadWic(device, cmdQueue, folder + info.File, true));
		_layerTileSizes.push_back(info.TileSize);
	}

	_layerCount = (UINT)_layerSources.size();
	assert(_layerCount != 0 && _layerCount <= TERRAIN_MAX_LAYERS);
}

void Terrain::CreateLayerArray(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
	const D3D12_RESOURCE_DESC first = _layerSources[0]->GetDesc();

	for (const auto& tex : _layerSources)
	{
		const D3D12_RESOURCE_DESC d = tex->GetDesc();
		assert(d.Width == first.Width && d.Height == first.Height);
		assert(d.Format == first.Format && d.MipLevels == first.MipLevels);
	}

	D3D12_RESOURCE_DESC arrayDesc = CD3DX12_RESOURCE_DESC::Tex2D(first.Format, first.Width, first.Height, (UINT16)_layerCount, first.MipLevels);
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &arrayDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(_layerArray.GetAddressOf())));

	std::vector<CD3DX12_RESOURCE_BARRIER> toSource;
	for (const auto& tex : _layerSources)
	{
		toSource.push_back(CD3DX12_RESOURCE_BARRIER::Transition(tex.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE));
	}
	cmdList->ResourceBarrier((UINT)toSource.size(), toSource.data());

	for (UINT layer = 0; layer < _layerCount; layer++)
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

void Terrain::CreateMaskTexture(ID3D12Device* device, ID3D12CommandQueue* cmdQueue)
{
	_maskCount = (_layerCount + 3) / 4;

	std::vector<uint8_t> pixels(TERRAIN_MASK_SIZE * TERRAIN_MASK_SIZE * 4 * _maskCount, 0);
	for (size_t i = 0; i < TERRAIN_MASK_SIZE * TERRAIN_MASK_SIZE; i++) { pixels[i * 4 + 0] = 255; }

	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, TERRAIN_MASK_SIZE, TERRAIN_MASK_SIZE, (UINT16)_maskCount, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(_maskArray.GetAddressOf())));

	const size_t sliceBytes = (size_t)TERRAIN_MASK_SIZE * TERRAIN_MASK_SIZE * 4;
	const LONG_PTR rowPitch = (LONG_PTR)TERRAIN_MASK_SIZE * 4;
	const LONG_PTR slicePitch = (LONG_PTR)sliceBytes;
	
	std::vector<D3D12_SUBRESOURCE_DATA> data(_maskCount);
	for (UINT i = 0; i < _maskCount; i++)
	{
		data[i].pData = pixels.data() + i * sliceBytes;
		data[i].RowPitch = rowPitch;
		data[i].SlicePitch = slicePitch;
	}

	ResourceUploadBatch upload(device);
	upload.Begin();
	upload.Upload(_maskArray.Get(), 0, data.data(), _maskCount);
	upload.Transition(_maskArray.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	upload.End(cmdQueue).wait();
}

void Terrain::CreateSRVAndUAV(ID3D12Device* device)
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

		device->CreateShaderResourceView(_heightMap.Get(), &srvDesc, _descRange.Cpu(0));
	}

	{
		const D3D12_RESOURCE_DESC desc = _layerArray.Get()->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
		srvDesc.Texture2DArray.ArraySize = _layerCount;
		srvDesc.Texture2DArray.FirstArraySlice = 0;
		srvDesc.Texture2DArray.MipLevels = desc.MipLevels;
		srvDesc.Texture2DArray.MostDetailedMip = 0;
		srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(_layerArray.Get(), &srvDesc, _descRange.Cpu(1));
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

		device->CreateShaderResourceView(_maskArray.Get(), &srvDesc, _descRange.Cpu(2));
	}
}

void Terrain::BuildTileMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
{
	std::vector<Vertex> vertices;
	vertices.reserve(TERRAIN_TILE_VERTS * TERRAIN_TILE_VERTS);

	Vertex vert = {};
	for (UINT z = 0; z < TERRAIN_TILE_VERTS; z++)
	{
		for (UINT x = 0; x < TERRAIN_TILE_VERTS; x++)
		{
			float u = x / float(TERRAIN_TILE_CELLS);
			float v = z / float(TERRAIN_TILE_CELLS);

			vert.Pos = { u, 0.0f, v };
			vert.Normal = { 0, 1, 0 };
			vert.Tangent = { 1, 0, 0, 1 };
			vert.TexC = { u,v };

			vertices.push_back(vert);
		}
	}

	std::vector<uint32_t> indices;
	indices.reserve(TERRAIN_TILE_CELLS * TERRAIN_TILE_CELLS * 6);

	for (UINT z = 0; z < TERRAIN_TILE_CELLS; z++)
	{
		for (UINT x = 0; x < TERRAIN_TILE_CELLS; x++)
		{
			uint32_t i0 = z * TERRAIN_TILE_VERTS + x;
			uint32_t i1 = i0 + 1;
			uint32_t i2 = i0 + TERRAIN_TILE_VERTS;
			uint32_t i3 = i2 + 1;

			indices.push_back(i0);
			indices.push_back(i2);
			indices.push_back(i1);

			indices.push_back(i1);
			indices.push_back(i2);
			indices.push_back(i3);
		}
	}

	_tileMesh = std::make_unique<MeshGeometry>();
 	_tileMesh->Name = "Terrain_Cell";

	UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(uint32_t);

	_tileMesh->VertexBufferGPU = D3DUtil::CreateDefaultBuffer(device, cmdList, vertices.data(), vbByteSize, _tileVbUpload);

	_tileMesh->IndexBufferGPU = D3DUtil::CreateDefaultBuffer(device, cmdList, indices.data(), ibByteSize, _tileIbUpload);

	_tileMesh->VertexByteStride = sizeof(Vertex);
	_tileMesh->VertexBufferByteSize = vbByteSize;
	_tileMesh->IndexBufferByteSize = ibByteSize;
	_tileMesh->IndexFormat = DXGI_FORMAT_R32_UINT;

	Submesh sub;
	sub.IndexCount = (UINT)indices.size();
	_tileMesh->Submeshes.push_back(sub);
}

std::unique_ptr<QuadNode> Terrain::BuildNode(XMFLOAT2 origin, float size, UINT depth)
{
	auto q = std::make_unique<QuadNode>();
	q->Origin = origin;
	q->Size = size;
	q->Depth = depth;

	if (depth < TERRAIN_MAX_DEPTH)
	{
		const float half = size * 0.5f;

		for (UINT i = 0; i < 4; i++)
		{
			XMFLOAT2 childOrigin =
			{
				origin.x + (i % 2) * half,
				origin.y + (i / 2) * half
			};

			q->Children[i] = BuildNode(childOrigin, half, depth + 1);
		}
	}

	return q;
}

void Terrain::UpdateLOD(const XMFLOAT3& eyePosWorld, float fovY, float screenHeight, const BoundingFrustum& frustum)
{
	_visibleTiles.clear();
	float screenK = screenHeight / (2.0f * tanf(fovY * 0.5f));
	SelectNode(_root.get(), eyePosWorld, screenK, &frustum, _visibleTiles);
}

void Terrain::UpdateShadowLOD(const XMFLOAT3& eye, float fovY, float screenHeight)
{
	_shadowTiles.clear();
	float screenK = screenHeight / (2.0f * tanf(fovY * 0.5f));
	SelectNode(_root.get(), eye, screenK, nullptr, _shadowTiles);
}

void Terrain::SelectNode(const QuadNode* node, const XMFLOAT3& eye, float screenK, const BoundingFrustum* frustum, std::vector<TerrainTile>& out)
{
	BoundingBox box = NodeBounds(node);

	if (frustum && frustum->Contains(box) == DirectX::DISJOINT) { return; }

	float dx = (std::max)(fabsf(eye.x - box.Center.x) - box.Extents.x, 0.0f);
	float dy = (std::max)(fabsf(eye.y - box.Center.y) - box.Extents.y, 0.0f);
	float dz = (std::max)(fabsf(eye.z - box.Center.z) - box.Extents.z, 0.0f);
	float dist = sqrtf(dx * dx + dy * dy + dz * dz);

	float splitDist = node->Size * screenK / (TERRAIN_TILE_CELLS * TERRAIN_MAX_SCREEN_ERROR);
	bool hasChildren = node->Children[0] != nullptr;

	if (dist < splitDist && hasChildren)
	{
		for (UINT i = 0; i < 4; i++)
		{
			SelectNode(node->Children[i].get(), eye, screenK, frustum, out);
		}
	}
	else
	{
		TerrainTile tile = {};
		tile.Origin = node->Origin;
		tile.Size = node->Size;
		tile.MorphStart = splitDist * 1.5f;
		tile.MorphEnd = splitDist * 2.0f;
		tile.Depth = node->Depth;

		out.push_back(tile);
	}
}

BoundingBox Terrain::NodeBounds(const QuadNode* node) const
{
	float half = node->Size * 0.5f;

	XMFLOAT3 center =
	{
		node->Origin.x + half + TERRAIN_WORLD_OFFSET,
		TERRAIN_HEIGHT_OFFSET + TERRAIN_HEIGHT_SCALE * 0.5f,
		node->Origin.y + half + TERRAIN_WORLD_OFFSET
	};

	XMFLOAT3 extents = { half, TERRAIN_HEIGHT_SCALE * 0.5f, half };

	return BoundingBox(center, extents);
}
