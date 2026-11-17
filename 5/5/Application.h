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
#include "D3DFramework.h"
#include "Editor.h"

class D3DFramework;

class Application
{
public:
    explicit Application(HINSTANCE hInstance);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize();
    int Run();

private:
    void OnResize(Resolution size);

    HINSTANCE _hInstance = nullptr;
    GameTimer _timer;

    std::unique_ptr<Window> _window;
    std::unique_ptr<Input> _input;
    std::unique_ptr<GraphicsDevice> _device;
    std::unique_ptr<AssetManager> _assets;
    std::unique_ptr<World> _world;
    std::unique_ptr<D3DFramework> _framework;
    std::unique_ptr<Editor> _editor;

private:
    RenderSettings _renderSettings;
};

#endif // !APPLICATION_HPP