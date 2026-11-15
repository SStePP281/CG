#ifndef SHADER_TYPES_HPP
#define SHADER_TYPES_HPP

#include <windows.h>
#include <DirectXMath.h>
#include "MathHelper.h"

struct alignas(16) MaterialConstants
{
	DirectX::XMFLOAT4X4 MatTransform = MathHelper::Identity4x4();

	DirectX::XMFLOAT4 DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };

	float Roughness = 0.5f;
	float Metallic = 0.0f;
	float NormalIntencity = 1.0f;
	float Pad0 = 0.0f;

	float MaxTessellationFactor = 1.0f;
	float MaxTessellationDistance = 0.0f;
	float Pad1[2] = { 0.0f, 0.0f };
};

struct alignas(16) LightConstants
{
	DirectX::XMFLOAT3 Strength = { 0.5f, 0.5f, 0.5f };
	float FalloffStart = 1.0f;

	DirectX::XMFLOAT3 Direction = { 0.0f, -1.0f, 0.0f };
	float FalloffEnd = 10.0f;

	DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
	float SpotPower = 64.0f;

	int LightType;
	int Pad[3];
};

struct alignas(16) LightInfoConstants
{
	UINT LightCount;
	UINT Pad[3];
};

#endif // !SHADER_TYPES_HPP