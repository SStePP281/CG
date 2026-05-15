#include "BaseD3DApp.h"
#include "ThrowIfFaild.h"
#include <iostream>
#include <string>

#include <d3d12.h>
#include <dxgi1_6.h>
#include <assert.h>

BaseD3DApp* BaseD3DApp::_app = nullptr;

BaseD3DApp::BaseD3DApp(HINSTANCE hInstance) : _hInstance(hInstance)
{
    assert(_app == nullptr);
	_app = this;
}

BaseD3DApp::~BaseD3DApp()
{
	if (_d3dDevice != nullptr)
	{
		FlushCommandQueue();
	}
}

void BaseD3DApp::Set4xMsaaState(bool value)
{
    if (_4xMsaaState != value)
    {
        _4xMsaaState = value;

        CreateSwapChain();
        OnResize();
    }
}

bool BaseD3DApp::Initialize()
{
    if (!InitMainWindow()) { return false; }
    if (!InitDirect3D()) { return false; }

    OnResize();
    return true;
}

bool BaseD3DApp::InitMainWindow()
{
   WNDCLASS wc;
	wc.style         = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc   = BaseD3DApp::MsgProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = _hInstance;
	wc.hIcon         = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor       = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wc.lpszMenuName  = 0;
	wc.lpszClassName = L"MainWnd";

	if (!RegisterClass(&wc))
	{
		MessageBox(0, L"RegisterClass Failed.", 0, 0);
		return false;
	}

	RECT R = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT};
    AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width  = R.right - R.left;
	int height = R.bottom - R.top;

	_hMainWnd = CreateWindow(L"MainWnd", _mainWndCaption.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height, 0, 0, _hInstance, 0); 
	if (!_hMainWnd)
	{
		MessageBox(0, L"CreateWindow Failed.", 0, 0);
		return false;
	}

	ShowWindow(_hMainWnd, SW_SHOW);
    UpdateWindow(_hMainWnd);

	return true;
}

bool BaseD3DApp::InitDirect3D()
{
#if defined(DEBUG) || defined(_DEBUG)
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        std::cout << "Debug layer enabled\n";
    }
#endif

    ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&_dxgiFactory)));
    ThrowIfFailed(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&_d3dDevice)));
    ThrowIfFailed(_d3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));

    _rtvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    _dsvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    _srvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
    msQualityLevels.Format = _backBufferFormat;
    msQualityLevels.SampleCount = 4;
    msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msQualityLevels.NumQualityLevels = 0;
    ThrowIfFailed(_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels)));

    _4xMsaaQuality = msQualityLevels.NumQualityLevels;
    assert(_4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

    CreateCommandObjects();
    CreateSwapChain();
    CreateDescriptorHeaps();

    return true;
}

void BaseD3DApp::CreateCommandObjects()
{
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

	ThrowIfFailed(_d3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_cmdQueue)));
	std::cout << "Command queue is created" << std::endl;

	ThrowIfFailed(_d3dDevice->CreateCommandAllocator(queueDesc.Type, IID_PPV_ARGS(_directCmdListAlloc.GetAddressOf())));
	std::cout << "Command allocatoris is created" << std::endl;

	ThrowIfFailed(_d3dDevice->CreateCommandList(0, queueDesc.Type, _directCmdListAlloc.Get(), nullptr, IID_PPV_ARGS(&_cmdList)));
	std::cout << "Command list is created" << std::endl;

	ThrowIfFailed(_cmdList->Close());
}

void BaseD3DApp::CreateSwapChain()
{
    _swapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd;
    sd.BufferDesc.Width = CLIENT_WIDTH;
    sd.BufferDesc.Height = CLIENT_HEIGHT;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = _backBufferFormat;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    sd.SampleDesc.Count = _4xMsaaState ? 4 : 1;
    sd.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = SWAP_CHAIN_BUFFER_COUNT;
    sd.OutputWindow = _hMainWnd;
    sd.Windowed = true;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ThrowIfFailed(_dxgiFactory->CreateSwapChain(_cmdQueue.Get(), &sd, _swapChain.GetAddressOf()));

	std::cout << "Swap chain is created" << std::endl;
}

void BaseD3DApp::CreateDescriptorHeaps()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = SWAP_CHAIN_BUFFER_COUNT;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(_rtvHeap.GetAddressOf())));


    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1;
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(_dsvHeap.GetAddressOf())));

    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NumDescriptors = 500;
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(_srvHeap.GetAddressOf())));
}

void BaseD3DApp::OnResize() 
{
    assert(_d3dDevice);
    assert(_swapChain);
    assert(_directCmdListAlloc);

    FlushCommandQueue();

    ThrowIfFailed(_cmdList->Reset(_directCmdListAlloc.Get(), nullptr));

    for (int i = 0; i < SWAP_CHAIN_BUFFER_COUNT; ++i) { _swapChainBuffer[i].Reset(); }
    _depthStencilBuffer.Reset();

    ThrowIfFailed(_swapChain->ResizeBuffers(SWAP_CHAIN_BUFFER_COUNT, CLIENT_WIDTH, CLIENT_HEIGHT, _backBufferFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    _currBackBuffer = 0;

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(_rtvHeap->GetCPUDescriptorHandleForHeapStart());
    for (UINT i = 0; i < SWAP_CHAIN_BUFFER_COUNT; i++)
    {
        ThrowIfFailed(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_swapChainBuffer[i])));
        _d3dDevice->CreateRenderTargetView(_swapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
        rtvHeapHandle.Offset(1, _rtvDescriptorSize);
    }

    D3D12_RESOURCE_DESC depthStencilDesc;
    depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthStencilDesc.Alignment = 0;
    depthStencilDesc.Width = CLIENT_WIDTH;
    depthStencilDesc.Height = CLIENT_HEIGHT;
    depthStencilDesc.DepthOrArraySize = 1;
    depthStencilDesc.MipLevels = 1;
 
    depthStencilDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;

    depthStencilDesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
    depthStencilDesc.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;
    depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE optClear;
    optClear.Format = _depthStencilFormat;
    optClear.DepthStencil.Depth = 1.0f;
    optClear.DepthStencil.Stencil = 0;
    auto propertie = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
    ThrowIfFailed(_d3dDevice->CreateCommittedResource(
        &propertie,
        D3D12_HEAP_FLAG_NONE,
        &depthStencilDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &optClear,
        IID_PPV_ARGS(_depthStencilBuffer.GetAddressOf())));

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = _depthStencilFormat;
    dsvDesc.Texture2D.MipSlice = 0;
    _d3dDevice->CreateDepthStencilView(_depthStencilBuffer.Get(), &dsvDesc, DepthStencilView());

    auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(_depthStencilBuffer.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    _cmdList->ResourceBarrier(1, &barrier);

    ThrowIfFailed(_cmdList->Close());
    ID3D12CommandList* cmdsLists[] = { _cmdList.Get() };
    _cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
    FlushCommandQueue();

    _screenViewport.TopLeftX = 0;
    _screenViewport.TopLeftY = 0;
    _screenViewport.Width = static_cast<float>(CLIENT_WIDTH);
    _screenViewport.Height = static_cast<float>(CLIENT_HEIGHT);
    _screenViewport.MinDepth = 0.0f;
    _screenViewport.MaxDepth = 1.0f;

    _scissorRect = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
}

int BaseD3DApp::Run()
{
    ShowWindow(_hMainWnd, SW_SHOW);
    UpdateWindow(_hMainWnd);
    _timer.Reset();

    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            _timer.Tick();

            if (!_appPaused)
            {
                CalculateFrameStats();
                Update(_timer);
                Draw(_timer);
            }
            else
            {
                Sleep(100);
            }
        }
    }

    return static_cast<int>(msg.wParam);
}

void BaseD3DApp::FlushCommandQueue()
{
    _currFence++;
    ThrowIfFailed(_cmdQueue->Signal(_fence.Get(), _currFence));

    if (_fence->GetCompletedValue() < _currFence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(_fence->SetEventOnCompletion(_currFence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }
}

void BaseD3DApp::CalculateFrameStats()
{
    static int frameCnt = 0;
    static float timeElapsed = 0.0f;

    frameCnt++;

    if ((_timer.TotalTime() - timeElapsed) >= 1.0f)
    {
        float fps = (float)frameCnt;
        float mspf = 1000.0f / fps;

        std::wstring windowText = _mainWndCaption +
            L"    FPS: " + std::to_wstring((int)fps) +
            L"    MS per Frame: " + std::to_wstring(mspf);

        SetWindowTextW(_hMainWnd, windowText.c_str());

        frameCnt = 0;
        timeElapsed += 1.0f;
    }
}

LRESULT BaseD3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BaseD3DApp* app = BaseD3DApp::GetApp();

    switch (msg)
    {
        case WM_CREATE:
        {
            CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreate->lpCreateParams));
            return 0;
        }

        case WM_LBUTTONDOWN:
        case WM_MBUTTONDOWN:
        case WM_RBUTTONDOWN:
        {
            if (app)
            {
                app->OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
        }

        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        {
            if (app)
            {
                app->OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            if (app)
            {
                app->OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            }
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
        {
            if (app && app->_d3dDevice)
            {
                app->_resizing = true;
                if (wParam == SIZE_MINIMIZED)
                {
                    app->_minimized = true;
                }
                else if (wParam == SIZE_MAXIMIZED)
                {
                    app->_maximized = true;
                }
                else if (wParam == SIZE_RESTORED)
                {
                    if (app->_minimized)
                    {
                        app->_minimized = false;
                    }
                    else if (app->_maximized)
                    {
                        app->_maximized = false;
                    }
                    else if (app->_resizing)
                    {
                        app->_resizing = false;
                    }

                    app->OnResize();
                }
            }
            return 0;
        }

        case WM_ACTIVATE:
        {
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                app->_appPaused = true;
                app->_timer.Stop();
            }
            else
            {
                app->_appPaused = false;
                app->_timer.Start();
            }

            return 0;
        }

        case WM_ENTERSIZEMOVE:
        {
            app->_appPaused = true;
            app->_resizing = true;
            app->_timer.Stop();

            return 0;
        }

        case WM_EXITSIZEMOVE:
        {
            app->_appPaused = false;
            app->_resizing = false;
            app->_timer.Start();

            app->OnResize();

            return 0;
        }

        case WM_DESTROY:
        {
            PostQuitMessage(0);
            return 0;
        }

        default:
        {
            return DefWindowProc(hwnd, msg, wParam, lParam);
        }
    }
}