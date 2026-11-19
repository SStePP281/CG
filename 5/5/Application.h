#ifndef APPLICATION_HPP
#define APPLICATION_HPP

#include <windows.h>
#include <memory>

#include "GameTimer.h"
#include "Window.h"
#include "Input.h"
#include "GraphicsDevice.h"
#include "AssetManager.h"
#include "World.h"
#include "Renderer.h"
#include "Editor.h"

class Renderer;

class Application
{
public:
    explicit Application(HINSTANCE hInstance);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize();

    void ApplyDisplaySettings();

    int Run();

private:
    void OnResize(Resolution size);

    HINSTANCE _hInstance = nullptr;
    GameTimer _timer;

private:
    RenderSettings _renderSettings;
    DisplaySettings _appliedDisplay;

private:
    std::unique_ptr<Window> _window;
    std::unique_ptr<Input> _input;
    std::unique_ptr<GraphicsDevice> _device;
    std::unique_ptr<AssetManager> _assets;
    std::unique_ptr<World> _world;
    std::unique_ptr<Renderer> _renderer;
    std::unique_ptr<Editor> _editor;
};

#endif // !APPLICATION_HPP