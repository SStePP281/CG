#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include <string>

#include <wrl/client.h>
#include <memory>
#include <vector>

#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXCollision.h>

#include "TerrainData.h"
#include "Mesh.h"
#include "GraphicsDevice.h"

using namespace DirectX;

using Microsoft::WRL::ComPtr;

constexpr float TERRAIN_TILE_WORLD_SIZE = 128.0f;
constexpr UINT TERRAIN_TILE_CELLS = 64;
constexpr UINT TERRAIN_TILE_VERTS = TERRAIN_TILE_CELLS + 1;
constexpr UINT TERRAIN_MAX_DEPTH = 5;
constexpr float TERRAIN_MAX_SCREEN_ERROR = 6.0f;
constexpr UINT MAX_TERRAIN_TILES = 4096;

struct TerrainTile
{ 
	XMFLOAT2 Origin;
	float Size;
	float MorphStart;
	float MorphEnd;
	UINT Depth;
};

struct QuadNode
{
	XMFLOAT2 Origin;
	float Size;
	UINT Depth;
	std::unique_ptr<QuadNode> Children[4];
};

class TerrainRenderer
{
public:
	static constexpr UINT STENCIL_TERRAIN = 1;

public:
	explicit TerrainRenderer(GraphicsDevice& device);

	void SetData(const TerrainData* data);
	const TerrainData* Data() const { return _data; }

	const MeshGeometry* GetTileMesh() const { return _tileMesh.get(); }

	void UpdateLOD(const XMFLOAT3& eyePosWorld, float fovY, float screenHeight, const BoundingFrustum& frustum);
	const std::vector<TerrainTile>& GetVisibleTiles() const { return _visibleTiles; }

	void UpdateShadowLOD(const XMFLOAT3& eye, float fovY, float screenHeight);
	const std::vector<TerrainTile>& GetShadowTiles() const { return _shadowTiles; }

private:
	void BuildTileMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
	std::unique_ptr<QuadNode> BuildNode(XMFLOAT2 origin, float size, UINT depth);
	void SelectNode(const QuadNode* node, const XMFLOAT3& eye, float screenK, const BoundingFrustum* frustum, std::vector<TerrainTile>& out);
	BoundingBox NodeBounds(const QuadNode* node) const;

private:
	const TerrainData* _data = nullptr;

	std::unique_ptr<MeshGeometry> _tileMesh;
	ComPtr<ID3D12Resource> _tileVbUpload;
	ComPtr<ID3D12Resource> _tileIbUpload;

	std::unique_ptr<QuadNode> _root;
	std::vector<TerrainTile> _visibleTiles;
	std::vector<TerrainTile> _shadowTiles;
};

#endif // !TERRAIN_HPP
