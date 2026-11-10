#ifndef ANTI_ALIASING_HPP
#define ANTI_ALIASING_HPP

#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXMath.h>
#include "RenderSettings.h"

constexpr UINT JITTER_COUNT = 8;

struct AATexture
{
    ID3D12Resource* resource = nullptr;
    UINT srvSlot = 0;
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
    ID3D12Device* device;
    ID3D12DescriptorHeap* srvHeap;
    UINT firstSlot;
    UINT srvDescSize;
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

    virtual AAMode GetMode() const { return AAMode::None; }

    virtual bool  UsesJitter()   const { return false; }
    virtual bool  UsesVelocity() const { return false; }
    virtual Resolution ComputeRenderResolution(const RenderSettings& s) const { return s.Display; }

    virtual UINT SrvSlotCount() const = 0;

    virtual void OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const RenderSettings& res) = 0;

    virtual void ResetHistory() {}

    virtual DirectX::XMFLOAT2 NextJitterPx() { return { 0.0f, 0.0f }; }

    virtual void Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in) = 0;

    virtual UINT OutputSrvSlot() const = 0;
};

#endif // !ANTI_ALIASING_HPP
