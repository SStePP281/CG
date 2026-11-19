#include "GraphicsDevice.h"
#include "ThrowIfFaild.h"
#include <iostream>
#include <cstring>
#include <algorithm>
#include <d3dx12.h>
#include <dxgi1_5.h>

GraphicsDevice::~GraphicsDevice()
{
    if (_device)
    {
        Flush();
    }

    if (_fenceEvent)
    {
        CloseHandle(_fenceEvent);
    }
}

void GraphicsDevice::Initialize(HWND hwnd, Resolution size, DXGI_FORMAT backBufferFormat)
{
    _backBufferFormat = backBufferFormat;

    CreateDevice();
    CreateCommandObjects();
    CreateDescriptorAllocators();
    CreateSwapChain(hwnd, size);
    CreateBackBufferViews();
}

void GraphicsDevice::CreateDevice()
{
#if defined(DEBUG) || defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        std::cout << "Debug layer enabled" << std::endl;
    }
#endif

    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&_factory)));
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&_device)));

#if defined(DEBUG) || defined(_DEBUG)
    ComPtr<ID3D12InfoQueue1> infoQueue1;
    if (SUCCEEDED(_device.As(&infoQueue1)))
    {
        DWORD cookie = 0;
        infoQueue1->RegisterMessageCallback(&GraphicsDevice::OnDebugMessage, D3D12_MESSAGE_CALLBACK_FLAG_NONE, nullptr, &cookie);
    }
    else
    {
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(_device.As(&infoQueue)))
        {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
    }
#endif

    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(_factory.As(&factory5)))
    {
        BOOL allow = FALSE;
        factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow));
        _tearingSupported = allow;
    }

    ThrowIfFailed(_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));
    _fenceEvent = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
}

void GraphicsDevice::CreateCommandObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ThrowIfFailed(_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_queue)));

    for (FrameContext& f : _frames)
    {
        ThrowIfFailed(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.Allocator)));
    }

    ThrowIfFailed(_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_immediateAllocator)));

    ThrowIfFailed(_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _immediateAllocator.Get(), nullptr, IID_PPV_ARGS(&_cmdList)));
    ThrowIfFailed(_cmdList->Close());
}

void GraphicsDevice::CreateDescriptorAllocators()
{
    _srv.Init(_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000, true);
    _rtv.Init(_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64, false);
    _dsv.Init(_device.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 16, false);
}

void GraphicsDevice::CreateSwapChain(HWND hwnd, Resolution size)
{
    DXGI_SWAP_CHAIN_DESC1 sd = {};
    sd.Width = size.Width;
    sd.Height = size.Height;
    sd.Format = _backBufferFormat;
    sd.SampleDesc = { 1, 0 };
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SWAPCHAIN_COUNT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Scaling = DXGI_SCALING_STRETCH;   // back buffer smaller than the window (fullscreen at lower resolution) is stretched to fit
    sd.Flags = SwapChainFlags();

    ComPtr<IDXGISwapChain1> swapChain1;
    ThrowIfFailed(_factory->CreateSwapChainForHwnd(_queue.Get(), hwnd, &sd, nullptr, nullptr, &swapChain1));
    ThrowIfFailed(swapChain1.As(&_swapChain));

    _factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    _backBuffers.resize(SWAPCHAIN_COUNT);
    _backBufferRtv = _rtv.Allocate(SWAPCHAIN_COUNT);
}

void GraphicsDevice::CreateBackBufferViews()
{
    for (UINT i = 0; i < SWAPCHAIN_COUNT; i++)
    {
        ThrowIfFailed(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_backBuffers[i])));
        _device->CreateRenderTargetView(_backBuffers[i].Get(), nullptr, _backBufferRtv.Cpu(i));
    }

    _backBufferIndex = 0;
}

void GraphicsDevice::ResizeSwapChain(Resolution size)
{
    Flush();

    for (auto& b : _backBuffers) { b.Reset(); }

    ThrowIfFailed(_swapChain->ResizeBuffers(SWAPCHAIN_COUNT, size.Width, size.Height, _backBufferFormat, SwapChainFlags()));

    CreateBackBufferViews();
}

Resolution GraphicsDevice::BackBufferSize() const
{
    const D3D12_RESOURCE_DESC desc = _backBuffers[0]->GetDesc();
    return { (UINT)desc.Width, desc.Height };
}

std::vector<Resolution> GraphicsDevice::SupportedResolutions() const
{
    ComPtr<IDXGIOutput> output;
    if (FAILED(_swapChain->GetContainingOutput(&output))) { return {}; }

    UINT count = 0;
    output->GetDisplayModeList(_backBufferFormat, 0, &count, nullptr);
    std::vector<DXGI_MODE_DESC> modes(count);
    output->GetDisplayModeList(_backBufferFormat, 0, &count, modes.data());

    std::vector<Resolution> result;
    for (const DXGI_MODE_DESC& m : modes)
    {
        Resolution r = { m.Width, m.Height };
        if (std::find(result.begin(), result.end(), r) == result.end()) { result.push_back(r); }   // the same size repeats for every refresh rate
    }

    std::sort(result.begin(), result.end(), [](const Resolution& a, const Resolution& b) { return a.Width * a.Height > b.Width * b.Height; });
    return result;
}

ID3D12GraphicsCommandList* GraphicsDevice::BeginFrame()
{
    _frameIndex = (_frameIndex + 1) % FRAME_COUNT;
    FrameContext& f = _frames[_frameIndex];

    WaitForFence(f.Fence);
    _release.Process(_fence->GetCompletedValue());

    ThrowIfFailed(f.Allocator->Reset());
    ThrowIfFailed(_cmdList->Reset(f.Allocator.Get(), nullptr));

    ID3D12DescriptorHeap* heaps[] = { _srv.Heap() };
    _cmdList->SetDescriptorHeaps(1, heaps);
    
    auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(BackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    _cmdList->ResourceBarrier(1, &toRT);

    return _cmdList.Get();
}

void GraphicsDevice::EndFrame()
{
    auto toPresent = CD3DX12_RESOURCE_BARRIER::Transition(BackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    _cmdList->ResourceBarrier(1, &toPresent);

    ThrowIfFailed(_cmdList->Close());

    ID3D12CommandList* lists[] = { _cmdList.Get() };
    _queue->ExecuteCommandLists(1, lists);

    UINT interval = _vsync ? 1 : 0;
    UINT flags = (!_vsync && _tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;
    ThrowIfFailed(_swapChain->Present(interval, flags));
    _backBufferIndex = (_backBufferIndex + 1) % SWAPCHAIN_COUNT;

    _frames[_frameIndex].Fence = ++_currentFence;
    ThrowIfFailed(_queue->Signal(_fence.Get(), _currentFence));
    _release.Submit(_currentFence);
}

ID3D12GraphicsCommandList* GraphicsDevice::BeginImmediate()
{
    ThrowIfFailed(_immediateAllocator->Reset());
    ThrowIfFailed(_cmdList->Reset(_immediateAllocator.Get(), nullptr));

    ID3D12DescriptorHeap* heaps[] = { _srv.Heap() };
    _cmdList->SetDescriptorHeaps(1, heaps);

    return _cmdList.Get();
}

void GraphicsDevice::EndImmediate()
{
    ThrowIfFailed(_cmdList->Close());

    ID3D12CommandList* lists[] = { _cmdList.Get() };
    _queue->ExecuteCommandLists(1, lists);

    ThrowIfFailed(_queue->Signal(_fence.Get(), ++_currentFence));
    _release.Submit(_currentFence);

    WaitForFence(_currentFence);
    _release.Process(_currentFence);
}

void GraphicsDevice::Flush()
{
    ThrowIfFailed(_queue->Signal(_fence.Get(), ++_currentFence));
    WaitForFence(_currentFence);
    _release.Process(_currentFence);
}

void GraphicsDevice::WaitForFence(UINT64 value)
{
    if (value == 0 || _fence->GetCompletedValue() >= value) { return; }

    ThrowIfFailed(_fence->SetEventOnCompletion(value, _fenceEvent));
    WaitForSingleObject(_fenceEvent, INFINITE);
}

void CALLBACK GraphicsDevice::OnDebugMessage(D3D12_MESSAGE_CATEGORY, D3D12_MESSAGE_SEVERITY severity, D3D12_MESSAGE_ID, LPCSTR description, void*)
{
    if (severity > D3D12_MESSAGE_SEVERITY_ERROR) { return; }
    if (strstr(description, "nv.ngx")) { return; }

    OutputDebugStringA("D3D12 ERROR: ");
    OutputDebugStringA(description);
    OutputDebugStringA("\n");

    std::cerr << "D3D12 ERROR: " << description << std::endl;

    __debugbreak();
}