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

Terrain::Terrain(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, ID3D12GraphicsCommandList* cmdList, CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle, UINT srvDescriptorSize)
	: _srvHandle(srvHandle), _srvDescriptorSize(srvDescriptorSize)
{
	LoadTextures(device, cmdQueue);
	CreateSRV(device);
	BuildTileMesh(device, cmdList);

	_root = BuildNode({ 0,0 }, TERRAIN_WORLD_SIZE, 0);
}

ComPtr<ID3D12Resource> Terrain::LoadWic(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const std::wstring& path, bool isColor)
{
	ComPtr<ID3D12Resource> res;

	ResourceUploadBatch resourceUpload(device);
	resourceUpload.Begin();

	ThrowIfFailed(CreateWICTextureFromFileEx(
		device, resourceUpload, path.c_str(),
		0, D3D12_RESOURCE_FLAG_NONE,
		isColor ? WIC_LOADER_FORCE_SRGB | WIC_LOADER_MIP_AUTOGEN : WIC_LOADER_DEFAULT,
		res.ReleaseAndGetAddressOf()));

	resourceUpload.End(cmdQueue).wait();

	return res;
}

void Terrain::LoadTextures(ID3D12Device* device, ID3D12CommandQueue* cmdQueue)
{
	const std::wstring folder = LOCAL_PATH_W + TERRAIN_FOLDER_W;

	_heightMap = LoadWic(device, cmdQueue, folder + L"Height.png", false);
	_colorMap = LoadWic(device, cmdQueue, folder + L"Color.png", true);

	const D3D12_RESOURCE_DESC desc = _heightMap->GetDesc();
	_width = (UINT)desc.Width;
	_height = desc.Height;

	assert(desc.Format == DXGI_FORMAT_R16_UNORM);
}

void Terrain::CreateSRV(ID3D12Device* device)
{
	ID3D12Resource* resources[] = { _heightMap.Get(), _colorMap.Get() };

	for (ID3D12Resource* res : resources)
	{
		const D3D12_RESOURCE_DESC desc = res->GetDesc();

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Format = desc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = desc.MipLevels;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		device->CreateShaderResourceView(res, &srvDesc, _srvHandle);
		_srvHandle.Offset(1, _srvDescriptorSize);
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

	_tileMesh->VertexBufferGPU = D3DUtil::CreateDefaultBuffer
	(
		device,
		cmdList,
		vertices.data(),
		vbByteSize,
		_tileMesh->VertexBufferUploader
	);

	_tileMesh->IndexBufferGPU = D3DUtil::CreateDefaultBuffer
	(
		device,
		cmdList,
		indices.data(),
		ibByteSize,
		_tileMesh->IndexBufferUploader
	);

	_tileMesh->VertexByteStride = sizeof(Vertex);
	_tileMesh->VertexBufferByteSize = vbByteSize;
	_tileMesh->IndexBufferByteSize = ibByteSize;
	_tileMesh->IndexFormat = DXGI_FORMAT_R32_UINT;

	SubmeshGeometry sub;
	sub.IndexCount = (UINT)indices.size();
	_tileMesh->DrawArgs["tile"] = sub;
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

	SelectNode(_root.get(), eyePosWorld, screenK, frustum);
}

void Terrain::SelectNode(const QuadNode* node, const XMFLOAT3& eye, float screenK, const BoundingFrustum& frustum)
{
	BoundingBox box = NodeBounds(node);

	if (frustum.Contains(box) == DirectX::DISJOINT) { return; }

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
			SelectNode(node->Children[i].get(), eye, screenK, frustum);
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

		_visibleTiles.push_back(tile);
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
