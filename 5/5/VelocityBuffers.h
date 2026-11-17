#ifndef VELOCITY_BUFFER_HPP
#define VELOCITY_BUFFER_HPP

#include "AntiAliasing.h"

#include <d3d12.h>
#include <d3dx12.h>
#include <DirectXMath.h>
#include "RenderSettings.h"
#include "GraphicsDevice.h"

using Microsoft::WRL::ComPtr;

class VelocityBuffers
{
public:
    static constexpr UINT SrvSlotCount = 2;

    VelocityBuffers(GraphicsDevice& device);
    void OnResize(GraphicsDevice& device, Resolution res);

    void Swap() { _current = 1 - _current; }

    AATexture Current()  const { return { _buffer[_current].Get(), _srvRange.Sub(_current) }; }
    AATexture Previous() const { return { _buffer[1 - _current].Get(), _srvRange.Sub(1 - _current) }; }
    DescriptorRange CurrentRTV() const { return _rtvRange.Sub(_current); }

    void ClearCurrent(ID3D12GraphicsCommandList* cmdList);
    void TransitCurrentToWrite(ID3D12GraphicsCommandList* cmdList);
    void TransitCurrentToRead(ID3D12GraphicsCommandList* cmdList);

private:
    void CreateViews(ID3D12Device* device);

    ComPtr<ID3D12Resource> _buffer[2];
    UINT _current = 1;

    DescriptorRange _srvRange;
    DescriptorRange _rtvRange;
};

#endif // !VELOCITY_BUFFER_HPP