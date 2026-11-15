#ifndef MESH_HPP
#define MESH_HPP

#include <string>
#include <vector>
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXMath.h>
#include <DirectXCollision.h>

using Microsoft::WRL::ComPtr;

struct Vertex
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT4 Tangent;
	DirectX::XMFLOAT2 TexC;
};

struct Submesh
{
	std::string Name;

	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	INT BaseVertexLocation = 0;

	DirectX::BoundingBox Bounds;
};

struct MeshGeometry
{
    std::string Name;

    ComPtr<ID3D12Resource> VertexBufferGPU;
    ComPtr<ID3D12Resource> IndexBufferGPU;

    UINT VertexByteStride = 0;
    UINT VertexBufferByteSize = 0;
    DXGI_FORMAT IndexFormat = DXGI_FORMAT_R32_UINT;
    UINT IndexBufferByteSize = 0;

    std::vector<Submesh> Submeshes;

    D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const
    {
        return { VertexBufferGPU->GetGPUVirtualAddress(), VertexBufferByteSize, VertexByteStride };
    }

    D3D12_INDEX_BUFFER_VIEW IndexBufferView() const
    {
        return { IndexBufferGPU->GetGPUVirtualAddress(), IndexBufferByteSize, IndexFormat };
    }
};

#endif // !MESH_HPP
