#include "Application.h"
#include "D3DFramework.h"

Application::Application(HINSTANCE hInstance) : _hInstance(hInstance) {}

Application::~Application() = default;

bool Application::Initialize()
{
    const Resolution startSize = { 1920, 1080 };
    const UINT swapChainBufferCount = 2;

    _window = std::make_unique<Window>(_hInstance, L"SStePP281", startSize);
    _window->OnInputMessage = [this](UINT m, WPARAM w, LPARAM l) { _input.OnMessage(m, w, l); };

    _device = std::make_unique<GraphicsDevice>();
    _device->Initialize(_window->Handle(), _window->Size(), swapChainBufferCount, DXGI_FORMAT_R8G8B8A8_UNORM);

    _framework = std::make_unique<D3DFramework>(*_window, _input, *_device);
    if (!_framework->Initialize()) { return false; }

    _window->OnResize = [this](Resolution s) { OnResize(s); };

    return true;
}

void Application::OnResize(Resolution size)
{
    if (size.Width == 0 || size.Height == 0) { return; }

    _device->ResizeSwapChain(size);
    _framework->OnDisplayResize(size);
}

int Application::Run()
{
    _timer.Reset();

    while (_window->PumpMessages())
    {
        if (_window->Minimized() || !_window->Active()) { Sleep(50); continue; }

        _timer.Tick();
        _framework->Update(_timer);
        _framework->Draw(_timer);
        _input.EndFrame();
    }

    return 0;
}