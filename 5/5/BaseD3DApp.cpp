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

    _renderSettings.Display = { 1920, 1080 };
    _renderSettings.Preset = UpscalePreset::DLAA;
    _renderSettings.RenderScale = 1.0f;
    UpdateRenderResolution();

    _shadowSettings = {};
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
        OnDisplayResize();
    }
}

bool BaseD3DApp::Initialize()
{
    if (!InitMainWindow()) { return false; }
    if (!InitDirect3D()) { return false; }

    OnDisplayResize();
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

	RECT R = { 0, 0, (LONG)_renderSettings.Display.Width, (LONG)_renderSettings.Display.Height };
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

    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityLevels;
    msQualityLevels.Format = _backBufferFormat;
    msQualityLevels.SampleCount = 4;
    msQualityLevels.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msQualityLevels.NumQualityLevels = 0;
    ThrowIfFailed(_d3dDevice->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msQualityLevels, sizeof(msQualityLevels)));

    _4xMsaaQuality = msQualityLevels.NumQualityLevels;
    assert(_4xMsaaQuality > 0 && "Unexpected MSAA quality level.");

    CreateCommandObjects();
    CreateDescriptorHeaps();
    CreateSwapChain();

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
    _swapChainBuffer.resize(_renderSettings.SwapChainBufferCount);
    _rtvAlloc->Free(_swapChainRtv);
    _swapChain.Reset();

    DXGI_SWAP_CHAIN_DESC sd;
    sd.BufferDesc.Width = _renderSettings.Display.Width;
    sd.BufferDesc.Height = _renderSettings.Display.Height;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Format = _backBufferFormat;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    sd.SampleDesc.Count = _4xMsaaState ? 4 : 1;
    sd.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.BufferCount = _renderSettings.SwapChainBufferCount;
    sd.OutputWindow = _hMainWnd;
    sd.Windowed = true;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    ThrowIfFailed(_dxgiFactory->CreateSwapChain(_cmdQueue.Get(), &sd, _swapChain.GetAddressOf()));

    _swapChainRtv = _rtvAlloc->Allocate(_renderSettings.SwapChainBufferCount);

	std::cout << "Swap chain is created" << std::endl;
}

void BaseD3DApp::CreateDescriptorHeaps()
{
    _srvAlloc = std::make_unique<DescriptorAllocator>();
    _srvAlloc->Init(_d3dDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1000, true);

    _rtvAlloc = std::make_unique<DescriptorAllocator>();
    _rtvAlloc->Init(_d3dDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64, false);

    _dsvAlloc = std::make_unique<DescriptorAllocator>();
    _dsvAlloc->Init(_d3dDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 16, false);
}

void BaseD3DApp::UpdateRenderResolution()
{
    _renderSettings.Render.Width = max(1u, (UINT)(_renderSettings.Display.Width * _renderSettings.RenderScale));
    _renderSettings.Render.Height = max(1u, (UINT)(_renderSettings.Display.Height * _renderSettings.RenderScale));
}

void BaseD3DApp::OnDisplayResize() 
{
    assert(_d3dDevice);
    assert(_swapChain);
    assert(_directCmdListAlloc);

    FlushCommandQueue();

    for (UINT i = 0; i < _renderSettings.SwapChainBufferCount; ++i) { _swapChainBuffer[i].Reset(); }

    ThrowIfFailed(_swapChain->ResizeBuffers(_renderSettings.SwapChainBufferCount, _renderSettings.Display.Width, _renderSettings.Display.Height, _backBufferFormat, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH));

    _currBackBuffer = 0;
    for (UINT i = 0; i < _renderSettings.SwapChainBufferCount; i++)
    {
        ThrowIfFailed(_swapChain->GetBuffer(i, IID_PPV_ARGS(&_swapChainBuffer[i])));
        _d3dDevice->CreateRenderTargetView(_swapChainBuffer[i].Get(), nullptr, _swapChainRtv.Cpu(i));
    }

    _screenViewport.TopLeftX = 0;
    _screenViewport.TopLeftY = 0;
    _screenViewport.Width = static_cast<float>(_renderSettings.Display.Width);
    _screenViewport.Height = static_cast<float>(_renderSettings.Display.Height);
    _screenViewport.MinDepth = 0.0f;
    _screenViewport.MaxDepth = 1.0f;

    _scissorRect = { 0, 0, (LONG)_renderSettings.Display.Width, (LONG)_renderSettings.Display.Height };

    OnRenderResize();
}

void BaseD3DApp::OnRenderResize()
{
    _renderViewport = _screenViewport;
    _renderViewport.Width = static_cast<float>(_renderSettings.Render.Width);
    _renderViewport.Height = static_cast<float>(_renderSettings.Render.Height);

    _renderScissor = { 0, 0, (LONG)_renderSettings.Render.Width, (LONG)_renderSettings.Render.Height };
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

LRESULT BaseD3DApp::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    BaseD3DApp* app = BaseD3DApp::GetApp();

    if (app)
    {
        return app->HandleMsg(hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT BaseD3DApp::HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
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
            OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }

        case WM_LBUTTONUP:
        case WM_MBUTTONUP:
        case WM_RBUTTONUP:
        {
            OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }

        case WM_MOUSEMOVE:
        {
            OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            return 0;
        }

        case WM_MOUSEWHEEL:
        {
            OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam));
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
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);

            if (wParam == SIZE_MINIMIZED)
            {
                _appPaused = true;
                _minimized = true;
                _maximized = false;
                return 0;
            }

            _renderSettings.Display = { width, height };
            UpdateRenderResolution();

            if (!_d3dDevice) { return 0; }

            if (wParam == SIZE_MAXIMIZED)
            {
                _appPaused = false;
                _minimized = false;
                _maximized = true;
                OnDisplayResize();
            }
            else if (wParam == SIZE_RESTORED)
            {
                if (_minimized)
                {
                    _appPaused = false;
                    _minimized = false;
                    OnDisplayResize();
                }
                else if (_maximized)
                {
                    _appPaused = false;
                    _maximized = false;
                    OnDisplayResize();
                }
                else if (_resizing)
                {
                }
                else
                {
                    OnDisplayResize();
                }
            }

            return 0;
        }

        case WM_ACTIVATE:
        {
            if (LOWORD(wParam) == WA_INACTIVE)
            {
                _appPaused = true;
                _timer.Stop();
            }
            else
            {
                _appPaused = false;
                _timer.Start();
            }

            return 0;
        }

        case WM_ENTERSIZEMOVE:
        {
            _appPaused = true;
            _resizing = true;
            _timer.Stop();

            return 0;
        }

        case WM_EXITSIZEMOVE:
        {
            _appPaused = false;
            _resizing = false;
            _timer.Start();

            OnDisplayResize();

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
