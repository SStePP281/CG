#ifndef RENDER_SETTINGS_HPP
#define RENDER_SETTINGS_HPP

#include <windows.h>
#include <DirectXMath.h>

struct Resolution
{
	UINT Width = 0;
	UINT Height = 0;

	DirectX::XMFLOAT2 ToFloat2() const { return { (float)Width, (float)Height }; }
	DirectX::XMFLOAT2 InvFloat2() const { return { 1.0f / Width, 1.0f / Height }; }

	Resolution operator*(float s) const
	{
		return { (UINT)(Width * s), (UINT)(Height * s) };
	}
};

enum class AAMode { None, TAA, DLAA };

enum class UpscalePreset { DLAA, Quality, Balanced, Performance, UltraPerformance, Custom };

struct RenderSettings
{
	AAMode Mode = AAMode::None;
	UpscalePreset Preset = UpscalePreset::DLAA;
	float RenderScale = 1.0f;

	Resolution Display;
	Resolution Render;
};

inline const wchar_t* ToString(UpscalePreset p)
{
	switch (p)
	{
		case UpscalePreset::DLAA:             return L"DLAA";
		case UpscalePreset::Quality:          return L"Quality";
		case UpscalePreset::Balanced:         return L"Balanced";
		case UpscalePreset::Performance:      return L"Performance";
		case UpscalePreset::UltraPerformance: return L"Ultra Perf";
		case UpscalePreset::Custom:           return L"Custom";
		default:                              return L"?";
	}
}

inline const wchar_t* ToString(AAMode m)
{
	switch (m)
	{
	case AAMode::None: return L"None";
	case AAMode::TAA:  return L"TAA";
	case AAMode::DLAA: return L"DLSS";
	}
	return L"?";
}

#endif // !RENDER_SETTINGS_HPP
