#ifndef MODEL_STRUCT_HPP
#define MODEL_STRUCT_HPP

#include <Windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <DirectXCollision.h>
#include <string>
#include <unordered_map>
#include "MathHelper.h"
#include "DescriptorHeapAllocator.h"
#include "Mesh.h"
#include "Texture.h"
#include "Material.h"
#include "Model.h"
#include "ShaderTypes.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

enum class LightType { Directional = 0, Point = 1, Spot = 2 };

struct Light
{
	int LightIndex = -1;

	LightConstants Data;
	bool IsActive = true;
	int NumFramesDirty = 3; //NUM_FRAME_RECOURCES
};

struct SceneObject
{
	Model* ModelData;
	XMFLOAT4X4 World;
};

#endif // !MODEL_STRUCT_HPP
