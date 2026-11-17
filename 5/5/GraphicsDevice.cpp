#include "GraphicsDevice.h"
#include "ThrowIfFaild.h"
#include <iostream>
#include <cstring>
#include <d3dx12.h>

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
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferDesc.Width = size.Width;
    sd.BufferDesc.Height = size.Height;
    sd.BufferDesc.RefreshRate = { 60, 1 };
    sd.BufferDesc.Format = _backBufferFormat;
    sd.SampleDesc = { 1, 0 };
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SWAPCHAIN_COUNT;
    sd.OutputWindow = hwnd;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ThrowIfFailed(_factory->CreateSwapChain(_queue.Get(), &sd, _swapChain.GetAddressOf()));

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

    ThrowIfFailed(_swapChain->ResizeBuffers(SWAPCHAIN_COUNT, size.Width, size.Height, _backBufferFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    CreateBackBufferViews();
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

    ThrowIfFailed(_swapChain->Present(0, 0));
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