#include "AtmosphereRenderer.h"
#include <algorithm>

static float SmoothStep(float a, float b, float x)
{
	float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}

void AtmosphereRenderer::Update(UploadBuffer<AtmosphereConstant>& cb)
{
	AtmosphereConstant ac = {};
	_hasSun = false;

	if (_data)
	{
		XMFLOAT3 toSun = _data->ToSun();
		XMFLOAT3 T = SunTransmittance(*_data);
		float fade = SmoothStep(SUN_FADE_START, SUN_FADE_END, toSun.y);

		_sun.Type = LightType::Directional;
		_sun.Direction = XMFLOAT3(-toSun.x, -toSun.y, -toSun.z);
		float E = _data->SunIntensity * _data->SunLightScale * fade;
		_sun.Color = XMFLOAT3(_data->SunColor.x * T.x * E, _data->SunColor.y * T.y * E, _data->SunColor.z * T.z * E);
		_sun.Active = fade > 0.0f;
		_hasSun = _sun.Active;

		ac.SunDir = toSun;
		ac.SunIntensity = _data->SunIntensity;
		ac.SunColor = _data->SunColor;
		ac.MieG = _data->MieG;
		XMStoreFloat3(&ac.BetaR, XMVectorScale(XMLoadFloat3(&_data->RayleighBeta), _data->RayleighScale));
		ac.BetaM = _data->MieBeta * _data->MieScale;
		ac.HeightR = _data->RayleighHeight;
		ac.HeightM = _data->MieHeight;
		ac.MetersPerUnit = _data->MetersPerUnit;
		ac.AtmFlags = 1 | (_data->AffectObjects ? 2 : 0);

		ac.IBLIntensity = _data->IBLIntensity;
	}
	else
	{
		ac.AtmFlags = 0;
		ac.IBLIntensity = 0.0f;   // no IBL maps are bound without an atmosphere: the light pass must not read t6-t8
	}

	cb.CopyData(0, ac);
}

float AtmosphereRenderer::AirMass(float cosZ)
{
	cosZ = std::clamp(cosZ, 0.0f, 1.0f);
	float a = DirectX::XMConvertToDegrees(acosf(cosZ));
	return 1.0f / (cosZ + 0.15f * powf(93.885f - a, -1.253f));
}

XMFLOAT3 AtmosphereRenderer::SunTransmittance(const AtmosphereData& d)
{
	XMFLOAT3 toSun = d.ToSun();
	float m = AirMass(toSun.y);

	float mie = d.MieBeta * d.MieScale * d.MieHeight * m;
	float k = d.RayleighScale * d.RayleighHeight * m;

	return XMFLOAT3(
		expf(-(d.RayleighBeta.x * k + mie)),
		expf(-(d.RayleighBeta.y * k + mie)),
		expf(-(d.RayleighBeta.z * k + mie)));
}
