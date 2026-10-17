#ifndef TERRAIN_HPP
#define TERRAIN_HPP

#include <string>

#include <wrl/client.h>
#include <memory>
#include <vector>
#include <d3d12.h>
#include <d3dx12.h>

#include "ModelStruct.h"

using namespace Microsoft::WRL;

constexpr float TERRAIN_WORLD_SIZE = 1024.0f;
constexpr float TERRAIN_HEIGHT_SCALE = 200.0f;
constexpr float TERRAIN_HEIGHT_OFFSET = -100.0f;
constexpr float TERRAIN_WORLD_OFFSET = -TERRAIN_WORLD_SIZE * 0.5f;

constexpr float TERRAIN_TILE_WORLD_SIZE = 128.0f;
constexpr UINT TERRAIN_TILE_CELLS = 64;
constexpr UINT TERRAIN_MAX_DEPTH = 5;
constexpr float TERRAIN_MAX_SCREEN_ERROR = 6.0f;

constexpr UINT TERRAIN_TILES_PER_SIDE = (UINT)(TERRAIN_WORLD_SIZE / TERRAIN_TILE_WORLD_SIZE);
constexpr UINT MAX_TERRAIN_TILES = 1024; 
constexpr UINT TERRAIN_TILE_VERTS = TERRAIN_TILE_CELLS + 1;

constexpr UINT TERRAIN_PAINT_TEXTURE_SIZE = 1024;

static_assert(TERRAIN_TILES_PER_SIDE * TERRAIN_TILE_WORLD_SIZE == TERRAIN_WORLD_SIZE, "World size must be a multiple of tile size");

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
	Terrain(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, ID3D12GraphicsCommandList* cmdList, CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle, UINT srvDescriptorSize);

	Terrain(const Terrain& rhs) = delete;
	Terrain& operator=(const Terrain& rhs) = delete;

	UINT GetSlotCount() { return 2 + _colorMaps.size(); }

	UINT GetWidth() const { return _width; }
	UINT GetHeight() const { return _height; }

	const MeshGeometry* GetTileMesh() const { return _tileMesh.get(); }

	void UpdateLOD(const XMFLOAT3& eyePosWorld, float fovY, float screenHeight, const BoundingFrustum& frustum);
	const std::vector<TerrainTile>& GetVisibleTiles() const { return _visibleTiles; }

private:
	void LoadTextures(ID3D12Device* device, ID3D12CommandQueue* cmdQueue);
	void CreateSRV(ID3D12Device* device);
	void BuildTileMesh(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

	std::unique_ptr<QuadNode> BuildNode(XMFLOAT2 origin, float size, UINT depth);

	static ComPtr<ID3D12Resource> LoadWic(ID3D12Device* device, ID3D12CommandQueue* cmdQueue, const std::wstring& path, bool isColor);

	void SelectNode(const QuadNode* node, const XMFLOAT3& eye, float screenK, const BoundingFrustum& frustum);
	BoundingBox NodeBounds(const QuadNode* node) const;

private:
	void CreateTerrainPaintTexture(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList);

private:
	std::unique_ptr<MeshGeometry> _tileMesh;

	std::unique_ptr<QuadNode> _root;
	std::vector<TerrainTile> _visibleTiles;

private:
	ComPtr<ID3D12Resource> _heightMap;
	std::unique_ptr<Texture> _paintTexture;
	std::vector<ComPtr<ID3D12Resource>> _colorMaps;

	CD3DX12_CPU_DESCRIPTOR_HANDLE _srvHandle;
	UINT _srvDescriptorSize = 0;

	UINT _width = 0;
	UINT _height = 0;
};

#endif // !TERRAIN_HPP
