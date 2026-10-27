#ifndef ANTI_ALIASING_HPP
#define ANTI_ALIASING_HPP

#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXMath.h>

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

struct AAResolution
{
    UINT renderW, renderH;
    UINT displayW, displayH;
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

enum class AAMode { None, TAA, DLAA };

class AntiAliasing
{
public:
    AntiAliasing() = default;
    AntiAliasing(const AntiAliasing&) = delete;
    AntiAliasing& operator=(const AntiAliasing&) = delete;
    virtual ~AntiAliasing() = default;

    virtual const char* Name() const = 0;

    virtual bool  UsesJitter()   const { return false; }
    virtual bool  UsesVelocity() const { return false; }
    virtual float RenderScale()  const { return 1.0f; }

    virtual UINT SrvSlotCount() const = 0;

    virtual void OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const AAResolution& res) = 0;

    virtual void ResetHistory() {}

    virtual DirectX::XMFLOAT2 NextJitterPx() { return { 0.0f, 0.0f }; }

    virtual void Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in) = 0;

    virtual UINT OutputSrvSlot() const = 0;
};

#endif // !ANTI_ALIASING_HPP
