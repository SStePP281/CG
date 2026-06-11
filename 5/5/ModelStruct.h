#ifndef MODEL_STRUCT_HPP
#define MODEL_STRUCT_HPP

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXCollision.h>
#include <string>
#include <unordered_map>
#include "MathHelper.h"

using namespace DirectX;
using namespace Microsoft::WRL;

struct Vertex
{
	XMFLOAT3 Pos;
	XMFLOAT3 Normal;
	XMFLOAT4 Tangent;
	XMFLOAT2 TexC;
};

struct SubmeshGeometry
{
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;

	BoundingBox Bounds;
};

struct MeshGeometry
{
	std::string Name;

	ComPtr<ID3DBlob> VertexBufferCPU = nullptr;
	ComPtr<ID3DBlob> IndexBufferCPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferGPU = nullptr;
	ComPtr<ID3D12Resource> IndexBufferGPU = nullptr;

	ComPtr<ID3D12Resource> VertexBufferUploader = nullptr;
	ComPtr<ID3D12Resource> IndexBufferUploader = nullptr;

	UINT VertexByteStride = 0;
	UINT VertexBufferByteSize = 0;
	DXGI_FORMAT IndexFormat = DXGI_FORMAT_R32_UINT;
	UINT IndexBufferByteSize = 0;

	std::unordered_map<std::string, SubmeshGeometry> DrawArgs;

	D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
	{
		D3D12_VERTEX_BUFFER_VIEW vbv;
		vbv.BufferLocation = VertexBufferGPU->GetGPUVirtualAddress();
		vbv.StrideInBytes = VertexByteStride;
		vbv.SizeInBytes = VertexBufferByteSize;

		return vbv;
	}

	D3D12_INDEX_BUFFER_VIEW IndexBufferView() const
	{
		D3D12_INDEX_BUFFER_VIEW ibv;
		ibv.BufferLocation = IndexBufferGPU->GetGPUVirtualAddress();
		ibv.Format = IndexFormat;
		ibv.SizeInBytes = IndexBufferByteSize;

		return ibv;
	}

	void DisposeUploaders()
	{
		VertexBufferUploader = nullptr;
		IndexBufferUploader = nullptr;
	}
};

enum class LightType { Directional = 0, Point = 1, Spot = 2 };

struct alignas(16) LightConstants
{
	XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
	float FalloffStart = 1.0f;
	XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
	float FalloffEnd = 10.0f;
	XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	float SpotPower = 64.0f;
	int LightType;
	int Pad[3];
};

struct Light
{
	int LightIndex = -1;

	LightConstants Data;
	bool IsActive = true;
	int NumFramesDirty = 3; //NUM_FRAME_RECOURCES
};

struct LightInfoConstants
{
	UINT LightCount;
	UINT Pad[3];
};

struct MaterialConstants
{
	XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

	XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };

	float Roughness = 0.5f;
	float Metallic = 0.0f;
	float NormalIntencity = 1.0f;
	float Pad0 = 0.0f;

	float MaxTessellationFactor = 1.0f;
	float MaxTessellationDistance = 0.0f;
	float Pad1[2] = { 0.0f, 0.0f };
};

struct Material
{
	std::string Name;

	int MatCBIndex = -1;

	int DiffuseSrvHeapIndex = 0;
	int NormalSrvHeapIndex = 1;
	int MetallicSrvHeapIndex = 2;
	int RoughnessSrvHeapIndex = 3;
	int AOSrvHeapIndex = 4;
	int DisplacementSrvHeapIndex = 5;

	int NumFramesDirty = 3;

	MaterialConstants Data;
};

struct Texture
{
	std::string Name;

	std::wstring Filename;

	ComPtr<ID3D12Resource> Resource = nullptr;
	ComPtr<ID3D12Resource> UploadHeap = nullptr;

	int SrvHeapIndex = -1;
};

struct Model
{
	MeshGeometry* Mesh;

	struct Part
	{
		std::string SubmeshName;
		std::string MaterialName;
	};

	std::vector<Part> Parts;
};

struct SceneObject
{
	Model* ModelData;
	XMFLOAT4X4 World;
};

#endif // !MODEL_STRUCT_HPP
