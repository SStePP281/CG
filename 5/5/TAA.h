#ifndef TAA_HPP
#define TAA_HPP

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

constexpr UINT JITTER_COUNT = 8;

class TAA
{
public:
	TAA(ID3D12Device* device, CD3DX12_CPU_DESCRIPTOR_HANDLE srvHandle, UINT srvDescriptorSize, UINT width, UINT height);

	TAA(const TAA& rhs) = delete;
	TAA& operator=(const TAA& rhs) = delete;

	UINT GetSrvSlotCount() { return 4; }

	void OnResize(ID3D12Device* device, UINT width, UINT height);

	void Swap() { _historyIndex = 1 - _historyIndex; }

	UINT GetPrevColorSRVOffset() { return _historyIndex; }
	UINT GetCurrentColorSRVOfsset() { return 1 - _historyIndex; }

	D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentColorRTV() { return _colorRTVs[1 - _historyIndex]; }

public:
	void TransitCurrentToWrite(ID3D12GraphicsCommandList* cmdList);
	void TransitCurrentToRead(ID3D12GraphicsCommandList* cmdList);

private:
	void CreateResources(ID3D12Device* device);
	void CreateSRVAndRTV(ID3D12Device* device);

public:
	XMFLOAT4X4 GetJitteredMatrix(XMFLOAT4X4 cameraViewProj);

private:
	float Halton(UINT index, UINT base);
	XMFLOAT2 Generate() 
	{ 
		UINT i = _frameCount + 1;
		return XMFLOAT2(2 * (Halton(i, 2) - 0.5f) / (float)_width,
						2 * (Halton(i, 3) - 0.5f) / (float)_height);
	}

private:
	std::vector<ComPtr<ID3D12Resource>> _colorBuffer;
	std::vector<ComPtr<ID3D12Resource>> _velocityBuffer;

	ComPtr<ID3D12DescriptorHeap> _taaRTVHeap;
	D3D12_CPU_DESCRIPTOR_HANDLE _colorRTVs[2];
	D3D12_CPU_DESCRIPTOR_HANDLE _velocityRTVs[2];

	CD3DX12_CPU_DESCRIPTOR_HANDLE _srvHandle;
	UINT _srvDescriptorSize = 0;
	UINT _rtvDescriptorSize = 0;

private:
	UINT _width;
	UINT _height;

	UINT _frameCount = 0;

	UINT _historyIndex = 0;
};

#endif // !TAA_HPP
