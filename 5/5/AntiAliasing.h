#ifndef ANTI_ALIASING_HPP
#define ANTI_ALIASING_HPP

#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXMath.h>
#include "RenderSettings.h"
#include "GraphicsDevice.h"

constexpr UINT JITTER_COUNT = 8;

struct AATexture
{
    ID3D12Resource* resource = nullptr;
    DescriptorRange srv;
};

struct AAInputs
{
    AATexture color;
    AATexture depth;
    AATexture velocity;
    AATexture prevVelocity;
};

struct AAInitContext
{
    GraphicsDevice& device;
    const D3D12_STATIC_SAMPLER_DESC* samplers;
    UINT samplerCount;
};

class AntiAliasing
{
public:
    AntiAliasing() = default;
    AntiAliasing(const AntiAliasing&) = delete;
    AntiAliasing& operator=(const AntiAliasing&) = delete;
    virtual ~AntiAliasing() = default;

    virtual AASettings::AAMode GetMode() const { return AASettings::AAMode::None; }

    virtual bool  UsesJitter()   const { return false; }
    virtual bool  UsesVelocity() const { return false; }
    virtual Resolution ComputeRenderResolution(AASettings& aaSettings, const Resolution& display) const { return display; }

    virtual void OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, AASettings& aaSettings, const Resolution& display) = 0;

    virtual void ResetHistory() {}

    virtual DirectX::XMFLOAT2 NextJitterPx() { return { 0.0f, 0.0f }; }

    virtual void Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in) = 0;
    
    virtual DescriptorRange OutputSrv() const = 0;
};

#endif // !ANTI_ALIASING_HPP
