#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include <string>

#include <wrl/client.h>
#include <memory>
#include <vector>
#include <d3d12.h>
#include <d3dx12.h>

#include <DirectXCollision.h>
#include "Mesh.h"
#include "GraphicsDevice.h"
using namespace DirectX;

using Microsoft::WRL::ComPtr;

constexpr float TERRAIN_WORLD_SIZE = 1024.0f;
constexpr float TERRAIN_HEIGHT_SCALE = 200.0f;
constexpr float TERRAIN_HEIGHT_OFFSET = -100.0f;
constexpr float TERRAIN_WORLD_OFFSET = -TERRAIN_WORLD_SIZE * 0.5f;

constexpr float TERRAIN_TILE_WORLD_SIZE = 128.0f;
constexpr UINT TERRAIN_TILE_CELLS = 64;

constexpr UINT TERRAIN_MAX_DEPTH = 5;
constexpr float TERRAIN_MAX_SCREEN_ERROR = 6.0f;

constexpr UINT TERRAIN_MASK_SIZE = 2048;
constexpr UINT TERRAIN_MAX_LAYERS = 16;

constexpr UINT BRUSH_RADIUS_MIN = 1, BRUSH_RADIUS_MAX = 200;
constexpr float BRUSH_STRENGTH_MIN = 0.05f, BRUSH_STRENGTH_MAX = 10.0f;

constexpr UINT TERRAIN_TILES_PER_SIDE = (UINT)(TERRAIN_WORLD_SIZE / TERRAIN_TILE_WORLD_SIZE);
constexpr UINT MAX_TERRAIN_TILES = 4096; 
constexpr UINT TERRAIN_TILE_VERTS = TERRAIN_TILE_CELLS + 1;

static_assert(TERRAIN_TILES_PER_SIDE * TERRAIN_TILE_WORLD_SIZE == TERRAIN_WORLD_SIZE, "World size must be a multiple of tile size");

struct TerrainHit
{
	XMFLOAT3 PosW;
	UINT Valid;

	XMFLOAT2 MapUV;
	XMFLOAT2 Pad;
};

struct LayerInfo
{
	std::wstring File;
	float TileSize;
};

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

class Terrain
{
public:
	Terrain(GraphicsDevice& device);

	Terrain(const Terrain& rhs) = delete;
	Terrain& operator=(const Terrain& rhs) = delete;

	UINT GetWidth() const { return _width; }
	UINT GetHeight() const { return _height; }

	UINT GetMaskCount() const { return _maskCount; }
	UINT GetLayerCount() const { return _layerCount; }

	ID3D12Resource* GetMaskArray() const { return _maskArray.Get(); }

	DescriptorRange TexturesGpu() const { return _descRange; }

	const std::vector<float>& GetLayerTileSizes() const { return _layerTileSizes; }

	const MeshGeometry* GetTileMesh() const { return _tileMesh.get(); }

	void UpdateLOD(const XMFLOAT3& eyePosWorld, float fovY, float screenHeight, const BoundingFrustum& frustum);
	const std::vector<TerrainTile>& GetVisibleTiles() const { return _visibleTiles; }

	void UpdateShadowLOD(const XMFLOAT3& eye, float fovY, float screenHeight);
	const std::vector<TerrainTile>& GetShadowTiles() const { return _shadowTiles; }

private:
	void LoadTextures(ID3D12Device* device, ID3D12CommandQueue* cmdQueue);

	void CreateLayerArray(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);
	void CreateMaskTexture(ID3D12Device* device, ID3D12CommandQueue* cmdQueue);

	void CreateSRVAndUAV(ID3D12Device* device);

	void BuildTileMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

	std::unique_ptr<QuadNode> BuildNode(XMFLOAT2 origin, float size, UINT depth);

	static ComPtr<ID3D12Resource> LoadWic(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const std::wstring& path, bool isColor);

	void SelectNode(const QuadNode* node, const XMFLOAT3& eye, float screenK, const BoundingFrustum* frustum, std::vector<TerrainTile>& out);

	BoundingBox NodeBounds(const QuadNode* node) const;

private:
	std::unique_ptr<MeshGeometry> _tileMesh;

	ComPtr<ID3D12Resource> _tileVbUpload;
	ComPtr<ID3D12Resource> _tileIbUpload;

	std::unique_ptr<QuadNode> _root;
	std::vector<TerrainTile> _visibleTiles;

	std::vector<TerrainTile> _shadowTiles;

private:
	ComPtr<ID3D12Resource> _heightMap;

	ComPtr<ID3D12Resource> _layerArray;
	std::vector<ComPtr<ID3D12Resource>> _layerSources;
	UINT _layerCount = 0;
	std::vector<float> _layerTileSizes;

	ComPtr<ID3D12Resource> _maskArray;
	UINT _maskCount = 0;

	DescriptorRange _descRange;

	UINT _width = 0;
	UINT _height = 0;
};

#endif // !TERRAIN_HPP
