#include "Application.h"
#include "Renderer.h"
#include "DemoScene.h"

Application::Application(HINSTANCE hInstance) : _hInstance(hInstance) {}

Application::~Application() = default;

bool Application::Initialize()
{
    _appliedDisplay = _renderSettings.Display;

    _input = std::make_unique<Input>();

    _window = std::make_unique<Window>(_hInstance, L"SStePP281", _renderSettings.Display);
    _window->OnInputMessage = [this](UINT m, WPARAM w, LPARAM l) { _input->OnMessage(m, w, l); };

    _device = std::make_unique<GraphicsDevice>();
    _device->SetVSync(_renderSettings.Display.VSync);
    Resolution backBuffer = _window->Fullscreen() ? _renderSettings.Display.WindowSize : _window->Size();
    _device->Initialize(_window->Handle(), backBuffer, DXGI_FORMAT_R8G8B8A8_UNORM);

    _assets = std::make_unique<AssetManager>(*_device);

    _world = std::make_unique<World>(*_assets);

    _device->BeginImmediate();
    BuildDemoScene(*_world, *_assets);
    _device->EndImmediate();

    _renderer = std::make_unique<Renderer>(*_window, *_input, *_assets, *_world, *_device, _renderSettings);
    if (!_renderer->Initialize()) { return false; }

    _editor = std::make_unique<Editor>(*_window, *_input, *_device, *_world, *_assets, *_renderer, _renderSettings);

    _window->OnResize = [this](Resolution s) { OnResize(s); };

    return true;
}

void Application::ApplyDisplaySettings()
{
    DisplaySettings& d = _renderSettings.Display;

    if (d.VSync != _appliedDisplay.VSync) { _device->SetVSync(d.VSync); }
    if (d.Fullscreen != _appliedDisplay.Fullscreen) { _window->SetFullscreen(d.Fullscreen); }
    if (d.WindowSize != _appliedDisplay.WindowSize)
    {
        // Windowed: resize the window, WM_SIZE brings us to OnResize.
        // Fullscreen: the window stays monitor-sized, only the back buffer changes.
        if (_window->Fullscreen()) { OnResize(_window->Size()); }
        else                       { _window->SetClientSize(d.WindowSize); }
    }

    _appliedDisplay = d;
}

void Application::OnResize(Resolution windowSize)
{
    if (windowSize.Width == 0 || windowSize.Height == 0) { return; }

    // Windowed: back buffer = client area. Fullscreen: back buffer = chosen resolution, DXGI stretches it over the monitor.
    Resolution backBuffer = _window->Fullscreen() ? _renderSettings.Display.WindowSize : windowSize;

    _device->ResizeSwapChain(backBuffer);
    _renderer->OnDisplayResize(backBuffer);

    if (!_window->Fullscreen())
    {
        // The user dragged the window: this is a fact, not a request, so don't apply it back.
        _renderSettings.Display.WindowSize = windowSize;
        _appliedDisplay.WindowSize = windowSize;
    }
}

int Application::Run()
{
    _timer.Reset();

    while (_window->PumpMessages())
    {
        if (_window->Minimized() || !_window->Active()) { Sleep(50); continue; }

        _timer.Tick();

        ApplyDisplaySettings();

        _editor->Update(_timer);
        _world->Update(_timer);

        _device->BeginFrame();
        {
            _renderer->Update(_timer);
            _editor->PreDraw(_timer);
            _renderer->Draw(_timer);
            _editor->Draw();
        }
        _device->EndFrame();

        _input->EndFrame();
    }

    return 0;
}