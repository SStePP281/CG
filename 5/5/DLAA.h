#ifndef DLAA_HPP
#define DLAA_HPP

#include "AntiAliasing.h"
#include "NGXManager.h"

#include <wrl/client.h>

using namespace Microsoft::WRL;
using namespace DirectX;

class DLAA final : public AntiAliasing
{
public:
    DLAA(ID3D12GraphicsCommandList* cmdList, NGXManager* ngxManager, const AAInitContext& context);
    ~DLAA() override;

    const char* Name() const override { return "DLAA"; }
    bool UsesJitter() const override { return true; }
    bool UsesVelocity() const override { return true; }
    UINT SrvSlotCount() const override { return 1; }

    void OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const AAResolution& res) override;
    void Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in) override;

    UINT OutputSrvSlot() const override { return _firstSlot; }

private:
    void InitializeFeature(ID3D12GraphicsCommandList* cmdList, const AAResolution& res);

public:
    void ResetHistory() override { _resetHistory = true; }

    virtual XMFLOAT2 NextJitterPx() override
    {
        UINT i = _frameCount + 1;
        _lastJitter = XMFLOAT2(Halton(i, 2) - 0.5f, Halton(i, 3) - 0.5f);
        _frameCount = (_frameCount + 1) % JITTER_COUNT;

        return XMFLOAT2(2.0f * _lastJitter.x / (float)_width,
                       -2.0f * _lastJitter.y / (float)_height);
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
    UINT _width;
    UINT _height;

    UINT _frameCount = 0;
    bool _resetHistory = true;

    bool _initialized = false;

private:
    ComPtr<ID3D12Resource> _output;

    ComPtr<ID3D12DescriptorHeap> _rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE _rtv;
    UINT _rtvDescriptorSize = 0;

    ID3D12DescriptorHeap* _srvHeap;
    UINT _firstSlot;
    UINT _srvDescSize;

private:
    XMFLOAT2 _lastJitter = { 0.0f, 0.0f };
};

#endif // !DLAA_HPP
