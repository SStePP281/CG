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
#include "RenderSettings.h"
#include "DescriptorHeapAllocator.h"
#include "FrameStats.h"

using namespace Microsoft::WRL;

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

	UINT64 GetCurrentFence() const { return _currFence; }
	UINT64 GetNextFence() const { return _currFence + 1; }

	float AspectRatio() const { return (float)_renderSettings.Display.Width / _renderSettings.Display.Height; }
	bool Get4xMsaaState() const { return _4xMsaaState; }
	void Set4xMsaaState(bool value);
	int Run();
	virtual bool Initialize();
	static LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

protected:
	virtual void CreateDescriptorHeaps();
	virtual void UpdateRenderResolution();
	virtual void OnDisplayResize();
	virtual void OnRenderResize();
	virtual void Update(const GameTimer& gt) = 0;
	virtual void Draw(const GameTimer& gt) = 0;

	virtual LRESULT HandleMsg(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	virtual void OnMouseDown(WPARAM btnState, int x, int y) {}
	virtual void OnMouseUp(WPARAM btnState, int x, int y) {}
	virtual void OnMouseMove(WPARAM btnState, int x, int y) {}
	virtual void OnMouseWheel(int delta) {}

protected:
	bool InitMainWindow();
	bool InitDirect3D();
	void CreateCommandObjects();
	void CreateSwapChain();
	void FlushCommandQueue();

	ID3D12Resource* CurrentBackBuffer() const { return _swapChainBuffer[_currBackBuffer].Get(); }
	DescriptorRange CurrentBackBufferRtv() const { return _swapChainRtv.Sub(_currBackBuffer); }

protected:
	RenderSettings _renderSettings;
	ShadowSettings _shadowSettings;

	FrameStats _frameStats;
	FrameTimeHistory _frameHistory;

	static BaseD3DApp* _app;
	HINSTANCE _hInstance = nullptr;
	HWND _hMainWnd = nullptr;

	bool _appPaused = false;
	bool _minimized = false;
	bool _maximized = false;
	bool _resizing = false;
	bool _fullscreenState = false;
	bool _4xMsaaState = true;
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
	DescriptorRange _swapChainRtv;
	std::vector<ComPtr<ID3D12Resource>> _swapChainBuffer;

	std::unique_ptr<DescriptorAllocator> _srvAlloc;
	std::unique_ptr<DescriptorAllocator> _rtvAlloc;
	std::unique_ptr<DescriptorAllocator> _dsvAlloc;

	D3D12_VIEWPORT _screenViewport;
	D3D12_RECT _scissorRect;

	D3D12_VIEWPORT _renderViewport;
	D3D12_RECT _renderScissor;

	std::wstring _mainWndCaption = L"SStePP281";
	D3D_DRIVER_TYPE _d3dDriverType = D3D_DRIVER_TYPE_HARDWARE;
	DXGI_FORMAT _backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
};

#endif // !D3DAPP_HPP
