#ifndef D3DAPP_HPP
#define D3DAPP_HPP

#include <string>

#include <Windows.h>
#include <Windowsx.h> 

#include <wrl/client.h>

#include <d3d12.h>
#include <dxgi1_6.h>      
#include <d3dx12.h>
#include "GameTimer.h"

using namespace Microsoft::WRL;

constexpr UINT CLIENT_WIDTH = 800;
constexpr UINT CLIENT_HEIGHT = 600;

constexpr int SWAP_CHAIN_BUFFER_COUNT = 2;

class BaseD3DApp
{
protected:
	BaseD3DApp(HINSTANCE hInstance);
	BaseD3DApp(const BaseD3DApp& dxd) = delete;
	BaseD3DApp& operator=(const BaseD3DApp& rhs) = delete;
	virtual ~BaseD3DApp();
public:
	static BaseD3DApp* GetApp() { return _app; }
	HINSTANCE AppInst() const { return _hInstance; }
	HWND MainWnd() const { return _hMainWnd; }

	ComPtr<ID3D12Device> GetDevice() const { return _d3dDevice; }
	ComPtr<ID3D12GraphicsCommandList> GetCommandList() const { return _cmdList; }

	constexpr float AspectRatio() const { return (float)CLIENT_WIDTH / CLIENT_HEIGHT; }
	bool Get4xMsaaState() const { return _4xMsaaState; }
	void Set4xMsaaState(bool value);
	int Run();
	virtual bool Initialize();
	static LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
protected:
	virtual void CreateDescriptorHeaps();
	virtual void OnResize();
	virtual void Update(const GameTimer& gt) = 0;
	virtual void Draw(const GameTimer& gt) = 0;
	virtual void OnMouseDown(WPARAM btnState, int x, int y) {}
	virtual void OnMouseUp(WPARAM btnState, int x, int y) {}
	virtual void OnMouseMove(WPARAM btnState, int x, int y) {}
protected:
	bool InitMainWindow();
	bool InitDirect3D();
	void CreateCommandObjects();
	void CreateSwapChain();
	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer() const { return _swapChainBuffer[_currBackBuffer].Get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const { return CD3DX12_CPU_DESCRIPTOR_HANDLE(_rtvHeap->GetCPUDescriptorHandleForHeapStart(), _currBackBuffer, _rtvDescriptorSize); }
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const { return _dsvHeap->GetCPUDescriptorHandleForHeapStart(); }
	
	void CalculateFrameStats();
protected:
	static BaseD3DApp* _app;
	HINSTANCE _hInstance = nullptr;
	HWND _hMainWnd = nullptr;

	bool _appPaused = false;
	bool _minimized = false;
	bool _maximized = false;
	bool _resizing = false;
	bool _fullscreenState = false;
	bool _4xMsaaState = false;
	UINT _4xMsaaQuality = 0;

	GameTimer _timer;
	ComPtr<IDXGIFactory4> _dxgiFactory;
	ComPtr<IDXGISwapChain> _swapChain;
	ComPtr<ID3D12Device> _d3dDevice;
	ComPtr<ID3D12Fence> _fence;
	UINT64 _currFence = 0;

	ComPtr<ID3D12CommandQueue> _cmdQueue;
	ComPtr<ID3D12CommandAllocator> _directCmdListAlloc;
	ComPtr<ID3D12GraphicsCommandList> _cmdList;

	int _currBackBuffer = 0;
	ComPtr<ID3D12Resource> _swapChainBuffer[SWAP_CHAIN_BUFFER_COUNT];
	ComPtr<ID3D12Resource> _depthStencilBuffer;

	ComPtr<ID3D12DescriptorHeap> _srvHeap;
	ComPtr<ID3D12DescriptorHeap> _rtvHeap;
	ComPtr<ID3D12DescriptorHeap> _dsvHeap;

	UINT _rtvDescriptorSize = 0;
	UINT _dsvDescriptorSize = 0;
	UINT _srvDescriptorSize = 0;

	D3D12_VIEWPORT _screenViewport;
	D3D12_RECT _scissorRect;

	std::wstring _mainWndCaption = L"D3D App";
	D3D_DRIVER_TYPE _d3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT _backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	DXGI_FORMAT _depthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
};

#endif // !D3DAPP_HPP
