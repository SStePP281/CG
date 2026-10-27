#ifndef TAA_HPP
#define TAA_HPP

#include "AntiAliasing.h"

#include <string>

#include <wrl/client.h>
#include <memory>
#include <vector>
#include <dxgiformat.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <d3dx12.h>

using namespace DirectX;
using namespace Microsoft::WRL;

class TAA final : public AntiAliasing
{
public:
	TAA(const AAInitContext& ctx);

	TAA(const TAA& rhs) = delete;
	TAA& operator=(const TAA& rhs) = delete;

	const char* Name() const override { return "TAA"; }
	virtual bool  UsesJitter()   const { return true; }
	virtual bool  UsesVelocity() const { return true; }
	UINT SrvSlotCount() const override { return 2; }

	UINT OutputSrvSlot() const override { return GetCurrentSlot(); }

	void OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const AAResolution& res) override;

	void Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in) override;

private:
	UINT GetPreviousSlot() const { return _firstSlot + 1 - _currentIndex; }
	UINT GetCurrentSlot() const { return _firstSlot + _currentIndex; }

private:
	void CreateRootsignature(const AAInitContext& ctx);
	void CreatePSO(const AAInitContext& ctx);

	void CreateResources(ID3D12Device* device);
	void CreateSRVAndRTV(ID3D12Device* device);

public:
	virtual XMFLOAT2 NextJitterPx() override
	{
		XMFLOAT2 jitter = Generate();
		_frameCount = (_frameCount + 1) % JITTER_COUNT;

		return jitter;
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

	XMFLOAT2 Generate() 
	{ 
		UINT i = _frameCount + 1;
		return XMFLOAT2(2 * (Halton(i, 2) - 0.5f) / (float)_width,
						2 * (Halton(i, 3) - 0.5f) / (float)_height);
	}

private:
	ComPtr<ID3D12Resource> _colorBuffer[2];
	UINT _currentIndex = 0;

	ComPtr<ID3D12DescriptorHeap> _taaRTVHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE _colorRTVs[2];
	UINT _rtvDescriptorSize = 0;

	ID3D12DescriptorHeap* _srvHeap;
	UINT _firstSlot;
	UINT _srvDescSize;

	ComPtr<ID3D12RootSignature> _rootSignature;
	ComPtr<ID3D12PipelineState> _pso;

private:
	UINT _width;
	UINT _height;

	UINT _frameCount = 0;
};

#endif // !TAA_HPP
