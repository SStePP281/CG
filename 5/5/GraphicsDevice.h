#ifndef GRAPHICS_DEVICE_HPP
#define GRAPHICS_DEVICE_HPP

#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>

#include "DescriptorHeapAllocator.h"
#include "DeferredReleaseQueue.h"
#include "RenderSettings.h"

using Microsoft::WRL::ComPtr;

class GraphicsDevice
{
public:
	static constexpr UINT FRAME_COUNT = 3;
    static constexpr UINT SWAPCHAIN_COUNT = 2;

    GraphicsDevice() = default;
    ~GraphicsDevice();

    GraphicsDevice(const GraphicsDevice&) = delete;
    GraphicsDevice& operator=(const GraphicsDevice&) = delete;

    void Initialize(HWND hwnd, Resolution size, DXGI_FORMAT backBufferFormat);

    void SetVSync(bool v) { _vsync = v; }

    ID3D12Device* Device() const { return _device.Get(); }
    ID3D12CommandQueue* Queue() const { return _queue.Get(); }
    ID3D12GraphicsCommandList* CommandList() const { return _cmdList.Get(); }

    ID3D12GraphicsCommandList* BeginFrame();
    void EndFrame();
    UINT FrameIndex() const { return _frameIndex; }

    ID3D12GraphicsCommandList* BeginImmediate();
    void EndImmediate();

    void Flush();

    void ResizeSwapChain(Resolution size);
    ID3D12Resource* BackBuffer() const { return _backBuffers[_backBufferIndex].Get(); }
    DescriptorRange BackBufferRtv() const { return _backBufferRtv.Sub(_backBufferIndex); }
    DXGI_FORMAT BackBufferFormat() const { return _backBufferFormat; }
    Resolution BackBufferSize() const;

    // Unique resolutions of the monitor the window is on, largest first.
    std::vector<Resolution> SupportedResolutions() const;

    DescriptorAllocator& Srv() { return _srv; }
    DescriptorAllocator& Rtv() { return _rtv; }
    DescriptorAllocator& Dsv() { return _dsv; }
    DeferredReleaseQueue& Release() { return _release; }
    DescriptorRange ReallocateSrv(DescriptorRange old) { _release.Push(nullptr, &_srv, old); return _srv.Allocate(old.Count()); }

private:
    UINT SwapChainFlags() const { return _tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0; }

    void CreateDevice();
    void CreateCommandObjects();
    void CreateDescriptorAllocators();
    void CreateSwapChain(HWND hwnd, Resolution size);
    void CreateBackBufferViews();
    void WaitForFence(UINT64 value);

    static void CALLBACK OnDebugMessage(D3D12_MESSAGE_CATEGORY category, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID id, LPCSTR description, void* context);

private:
    struct FrameContext
    {
        ComPtr<ID3D12CommandAllocator> Allocator;
        UINT64 Fence = 0;
    };

    ComPtr<IDXGIFactory4> _factory;
    ComPtr<ID3D12Device> _device;
    ComPtr<ID3D12CommandQueue> _queue;
    ComPtr<ID3D12GraphicsCommandList> _cmdList;
    ComPtr<ID3D12CommandAllocator> _immediateAllocator;

    ComPtr<ID3D12Fence> _fence;
    UINT64 _currentFence = 0;
    HANDLE _fenceEvent = nullptr;

    FrameContext _frames[FRAME_COUNT];
    UINT _frameIndex = 0;

    bool _vsync = false;
    bool _tearingSupported = false;

    ComPtr<IDXGISwapChain> _swapChain;
    std::vector<ComPtr<ID3D12Resource>> _backBuffers;
    DescriptorRange _backBufferRtv;
    UINT _backBufferIndex = 0;
    DXGI_FORMAT _backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    DescriptorAllocator _srv;
    DescriptorAllocator _rtv;
    DescriptorAllocator _dsv;
    DeferredReleaseQueue _release;
};

#endif // !GRAPHICS_DEVICE_HPP