#include "TerrainRenderer.h"

#include <cassert>

#include "PathManager.h"
#include "ThrowIfFaild.h"
#include "WICTextureLoader.h"
#include "ResourceUploadBatch.h"
#include <algorithm>
#include <cmath>
#include "D3DUtil.h"

using namespace DirectX;

TerrainRenderer::TerrainRenderer(GraphicsDevice& device)
{
	BuildTileMesh(device.Device(), device.CommandList());
}

void TerrainRenderer::SetData(const TerrainData* data)
{
	_builtWorldSize = data ? data->WorldSize() : 0.0f;
	_data = data;
	_root = data ? BuildNode({ 0, 0 }, data->WorldSize(), 0) : nullptr;

	_visibleTiles.clear();
	_shadowTiles.clear();
}

void TerrainRenderer::BuildTileMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList)
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

std::unique_ptr<QuadNode> TerrainRenderer::BuildNode(XMFLOAT2 origin, float size, UINT depth)
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

void TerrainRenderer::UpdateLOD(const XMFLOAT3& eyePosWorld, float fovY, float screenHeight, const BoundingFrustum& frustum)
{
	_visibleTiles.clear();
	float screenK = screenHeight / (2.0f * tanf(fovY * 0.5f));
	SelectNode(_root.get(), eyePosWorld, screenK, &frustum, _visibleTiles);
}

void TerrainRenderer::UpdateShadowLOD(const XMFLOAT3& eye, float fovY, float screenHeight)
{
	_shadowTiles.clear();
	float screenK = screenHeight / (2.0f * tanf(fovY * 0.5f));
	SelectNode(_root.get(), eye, screenK, nullptr, _shadowTiles);
}

void TerrainRenderer::SelectNode(const QuadNode* node, const XMFLOAT3& eye, float screenK, const BoundingFrustum* frustum, std::vector<TerrainTile>& out)
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

BoundingBox TerrainRenderer::NodeBounds(const QuadNode* node) const
{
	float half = node->Size * 0.5f;
	float hs = _data->HeightScale();

	XMFLOAT3 center =
	{
		node->Origin.x + half + _data->WorldOffset(),
		_data->HeightOffset() + hs * 0.5f,
		node->Origin.y + half + _data->WorldOffset()
	};

	XMFLOAT3 extents = { half, fabsf(hs) * 0.5f, half };

	return BoundingBox(center, extents);
}