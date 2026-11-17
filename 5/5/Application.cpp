#include "Application.h"
#include "D3DFramework.h"
#include "DemoScene.h"

Application::Application(HINSTANCE hInstance) : _hInstance(hInstance) {}

Application::~Application() = default;

bool Application::Initialize()
{
    _input = std::make_unique<Input>();

    _window = std::make_unique<Window>(_hInstance, L"SStePP281", _renderSettings.Display.WindowSize);
    _window->OnInputMessage = [this](UINT m, WPARAM w, LPARAM l) { _input->OnMessage(m, w, l); };

    _device = std::make_unique<GraphicsDevice>();
    _device->Initialize(_window->Handle(), _window->Size(), DXGI_FORMAT_R8G8B8A8_UNORM);

    _assets = std::make_unique<AssetManager>(*_device);

    _world = std::make_unique<World>(*_assets);

    _device->BeginImmediate();
    BuildDemoScene(*_world, *_assets);
    _device->EndImmediate();

    _framework = std::make_unique<D3DFramework>(*_window, *_input, *_assets, *_world, *_device, _renderSettings);
    if (!_framework->Initialize()) { return false; }

    _editor = std::make_unique<Editor>(*_window, *_input, *_device, *_world, *_assets, *_framework, _renderSettings);

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

        _editor->Update(_timer);
        _world->Update(_timer);

        _device->BeginFrame();
        {
            _framework->Update(_timer);
            _editor->PreDraw(_timer);
            _framework->Draw(_timer);
            _editor->Draw();
        }
        _device->EndFrame();

        _input->EndFrame();
    }

    return 0;
}