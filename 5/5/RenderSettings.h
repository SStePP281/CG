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

	bool operator==(const Resolution& other) const
	{
		return Width == other.Width && Height == other.Height;
	}

	bool operator!=(const Resolution& other) const
	{
		return !(*this == other);
	}
};

struct DisplaySettings
{
	Resolution WindowSize = { 1920, 1080 };
	bool Fullscreen = false;
	bool VSync = false;

	bool operator==(const DisplaySettings& other) const
	{
		return WindowSize == other.WindowSize && Fullscreen == other.Fullscreen && VSync == other.VSync;
	}

	bool operator!=(const DisplaySettings& other) const
	{
		return !(*this == other);
	}
};

struct ShadowSettings
{
	bool EnableShadow = true;
	bool EnableTerrainShadow = false;

	UINT ShadowTextureSize = 4096;

	bool operator==(const ShadowSettings& other) const
	{
		return ShadowTextureSize == other.ShadowTextureSize;
	}

	bool operator!=(const ShadowSettings& other) const
	{
		return !(*this == other);
	}
};

struct AASettings
{
	enum class AAMode { None, TAA, DLAA };

	enum class UpscalePreset { DLAA, Quality, Balanced, Performance, UltraPerformance, Custom };

	AAMode Mode = AAMode::None;
	UpscalePreset Preset = UpscalePreset::DLAA;
	float RenderScale = 1.0f;

	bool operator==(const AASettings& other) const
	{
		return Mode == other.Mode && Preset == other.Preset && RenderScale == other.RenderScale;
	}

	bool operator!=(const AASettings& other) const
	{
		return !(*this == other);
	}
};

struct RenderSettings
{
	DisplaySettings Display;

	ShadowSettings Shadow;
	AASettings AA;

	bool operator==(const RenderSettings& other) const
	{
		return Shadow == other.Shadow && AA == other.AA;
	}

	bool operator!=(const RenderSettings& other) const
	{
		return !(*this == other);
	}
};

inline const wchar_t* ToString(AASettings::UpscalePreset p)
{
	switch (p)
	{
		case AASettings::UpscalePreset::DLAA:             return L"DLAA";
		case AASettings::UpscalePreset::Quality:          return L"Quality";
		case AASettings::UpscalePreset::Balanced:         return L"Balanced";
		case AASettings::UpscalePreset::Performance:      return L"Performance";
		case AASettings::UpscalePreset::UltraPerformance: return L"Ultra Perf";
		case AASettings::UpscalePreset::Custom:           return L"Custom";
		default:                              return L"?";
	}
}

inline const wchar_t* ToString(AASettings::AAMode m)
{
	switch (m)
	{
	case AASettings::AAMode::None: return L"None";
	case AASettings::AAMode::TAA:  return L"TAA";
	case AASettings::AAMode::DLAA: return L"DLSS";
	}
	return L"?";
}

#endif // !RENDER_SETTINGS_HPP
