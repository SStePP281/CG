#ifndef ATMOSPHERE_DATA_HPP
#define ATMOSPHERE_DATA_HPP

#include <DirectXMath.h>
#include <cmath>
#include <string>

#include "GraphicsDevice.h"
#include "SlotHandle.h"

using XMFLOAT3 = DirectX::XMFLOAT3;

struct AtmosphereDesc
{
	std::wstring IrradianceFileName;
	std::wstring SpecularFileName;
	std::wstring BrdfFileName;
};

class AtmosphereData
{
public:
	AtmosphereData(GraphicsDevice& device, const AtmosphereDesc& desc);

	AtmosphereData(const AtmosphereData&) = delete;
	AtmosphereData& operator=(const AtmosphereData&) = delete;

	DescriptorRange Srv() const { return _srv; }

public:
	float SunAzimuth = 0.0f;
	float SunElevation = 30.0f;
	float SunIntensity = 20.0f;
	float SunLightScale = 0.15f;
	XMFLOAT3 SunColor = XMFLOAT3(1.0f, 1.0f, 1.0f);

	XMFLOAT3 RayleighBeta = XMFLOAT3(5.8e-6f, 13.5e-6f, 33.1e-6f);
	float RayleighScale = 1.0f;
	float MieBeta = 2e-5f;
	float MieScale = 1.0f;
	float MieG = 0.8f;
	float RayleighHeight = 8400.0f;
	float MieHeight = 1250.0f;

	float MetersPerUnit = 1.0f;
	bool AffectObjects = true;

	float IBLIntensity = 1.0f;

private:
	AtmosphereDesc _desc;

	ComPtr<ID3D12Resource> _iblIrradiance;
	ComPtr<ID3D12Resource> _iblSpecular;
	ComPtr<ID3D12Resource> _iblBrdf;
	DescriptorRange _srv;

private:
	void LoadTextures(GraphicsDevice& device);

public:
	XMFLOAT3 ToSun() const 
	{ 
		float azim = DirectX::XMConvertToRadians(SunAzimuth);
		float elev = DirectX::XMConvertToRadians(SunElevation);

		return XMFLOAT3(cos(elev) * sin(azim), sin(elev), cos(elev) * cos(azim));
	}

	void Clear()
	{
		RayleighBeta = XMFLOAT3(5.8e-6f, 13.5e-6f, 33.1e-6f);
		RayleighScale = 1.0f;
		RayleighHeight = 8400.0f;

		MieBeta = 2e-5f;
		MieScale = 1.0f;
		MieG = 0.8f;
		MieHeight = 1250.0f;
	}
};
using AtmosphereHandle = SlotHandle<AtmosphereData>;

#endif // !ATMOSPHERE_DATA_HPP
