#ifndef MODEL_PARSE_HPP
#define MODEL_PARSE_HPP

#include <Windows.h>
#include <cstdint>
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <vector>
#include <unordered_map>
#include <string>

#include "MathHelper.h"
#include "ModelStruct.h"

using namespace DirectX;

class ModelParse
{
public:

	using uint16 = std::uint16_t;
	using uint32 = std::uint32_t;

	struct SubmeshInfo
	{
		std::string Name;
		std::string MaterialName;
		BoundingBox Bounds;

		UINT VertexOffset = 0;
		UINT IndexOffset = 0;
		UINT IndexCount = 0;
	};

	struct MaterialInfo
	{
		std::string Name;
		std::string DiffuseTextureName;
		std::string NormalTextureName;
		std::string DisplacementTextureName;
		XMFLOAT4 DiffuseColor = { 1,1,1,1 };
		XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();
	};

	struct MeshInfo
	{
		std::string MeshName;
		std::vector<Vertex> Vertices;
		std::vector<uint32_t> Indices32;

		std::vector<SubmeshInfo> Submeshes;
		std::unordered_map<std::string, MaterialInfo> Materials;
	};

	MeshInfo LoadOBJ(const std::string& path);
};

#endif // !MODEL_PARSE_HPP
