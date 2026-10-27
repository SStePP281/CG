#ifndef VELOCITY_BUFFER_HPP
#define VELOCITY_BUFFER_HPP

#include "AntiAliasing.h"

#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXMath.h>

using namespace Microsoft::WRL;

class VelocityBuffers
{
public:
    static constexpr UINT SrvSlotCount = 2;

    VelocityBuffers(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT firstSlot, UINT srvDescSize);
    void OnResize(ID3D12Device* device, UINT w, UINT h);

    void Swap() { _current = 1 - _current; }

    AATexture Current()  const { return { _buffer[_current].Get(),     _firstSlot + _current }; }
    AATexture Previous() const { return { _buffer[1 - _current].Get(), _firstSlot + 1 - _current }; }
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentRTV() const { return _rtv[_current]; }

    void ClearCurrent(ID3D12GraphicsCommandList* cmdList);
    void TransitCurrentToWrite(ID3D12GraphicsCommandList* cmdList);
    void TransitCurrentToRead(ID3D12GraphicsCommandList* cmdList);

private:
    void CreateViews(ID3D12Device* device);

    ComPtr<ID3D12Resource> _buffer[2];
    UINT _current = 1;

    ComPtr<ID3D12DescriptorHeap> _rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE _rtv[2];
    UINT _rtvDescSize;

    ID3D12DescriptorHeap* _srvHeap;
    UINT _firstSlot;
    UINT _srvDescSize;
};

#endif // !VELOCITY_BUFFER_HPP