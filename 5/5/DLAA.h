#ifndef DLAA_HPP
#define DLAA_HPP

#include "AntiAliasing.h"
#include "NGXManager.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class DLAA final : public AntiAliasing
{
public:
    DLAA(NGXManager* ngxManager, const AAInitContext& context);
    ~DLAA() override;

    AASettings::AAMode GetMode() const override { return AASettings::AAMode::DLAA; }

    bool UsesJitter() const override { return true; }
    bool UsesVelocity() const override { return true; }

    virtual Resolution ComputeRenderResolution(AASettings& aaSettings, const Resolution& display) const override;

    void OnResize(GraphicsDevice& device, AASettings& aaSettings, const Resolution& display) override;
    void Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in) override;

    DescriptorRange OutputSrv() const override { return _srvRange; }

private:
    void InitializeFeature(ID3D12GraphicsCommandList* cmdList);

public:
    void ResetHistory() override { _resetHistory = true; }

    virtual XMFLOAT2 NextJitterPx() override
    {
        UINT i = _frameCount + 1;
        _lastJitter = XMFLOAT2(Halton(i, 2) - 0.5f, Halton(i, 3) - 0.5f);
        _frameCount = (_frameCount + 1) % JITTER_COUNT;

        return XMFLOAT2(2.0f * _lastJitter.x / (float)_render.Width,
                       -2.0f * _lastJitter.y / (float)_render.Height);
    }

private:
    float Halton(UINT index, UINT base)
    {
        float f = 1.0f;
        float result = 0.0f;

        while (index > 0)
        {
            f /= (float)base;
            result += f * (float)(index % base);
            index = (UINT)(floorf((float)index / base));
        }

        return result;
    }

private:
    void CreateResources(ID3D12Device* device);
    void CreateSRVAndRTV(ID3D12Device* device);

private:
    NGXManager* _ngxManager;
    NVSDK_NGX_Handle* _featureHandle = nullptr;

private:
    Resolution _render;
    Resolution _display;
    NVSDK_NGX_PerfQuality_Value _quality = NVSDK_NGX_PerfQuality_Value_DLAA;

    UINT _frameCount = 0;
    bool _resetHistory = true;

    bool _initialized = false;

private:
    ComPtr<ID3D12Resource> _output;

    DescriptorRange _srvRange;

private:
    XMFLOAT2 _lastJitter = { 0.0f, 0.0f };

private:
    bool _needCreateFeature = false;

    static NVSDK_NGX_PerfQuality_Value ToNGXQuality(AASettings::UpscalePreset p)
    {
        switch (p)
        {
        case AASettings::UpscalePreset::Quality:          return NVSDK_NGX_PerfQuality_Value_MaxQuality;
        case AASettings::UpscalePreset::Balanced:         return NVSDK_NGX_PerfQuality_Value_Balanced;
        case AASettings::UpscalePreset::Performance:      return NVSDK_NGX_PerfQuality_Value_MaxPerf;
        case AASettings::UpscalePreset::UltraPerformance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
        case AASettings::UpscalePreset::DLAA:
        default:                              return NVSDK_NGX_PerfQuality_Value_DLAA;
        }
    }
};

#endif // !DLAA_HPP
