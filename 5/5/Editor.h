#ifndef EDITOR_HPP
#define EDITOR_HPP

#include "Window.h"
#include "Input.h"
#include "GraphicsDevice.h"
#include "World.h"
#include "AssetManager.h"
#include "D3DFramework.h"
#include "RenderGUI.h"
#include "IEditorTool.h"
#include "ProjectSettingsPanel.h"

class Editor
{
public:
    Editor(Window& window, Input& input, GraphicsDevice& device, World& world, AssetManager& asset, D3DFramework& framework, RenderSettings& renderSettings);

    void Update(GameTimer& gt);
    void PreDraw(GameTimer& gt);
    void Draw();

public:
    void DrawToolbar();

    template<class T, class... Args>
    T& AddTool(Args&&... args)
    {
        auto tool = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *tool;
        _tools.push_back(std::move(tool));
        return ref;
    }

    void SetActiveTool(IEditorTool* tool);

private:
    void UpdateCameraTransform(GameTimer& gt);

private:
    Window* _window;
    Input* _input;
    GraphicsDevice* _device;
    World* _world;
    AssetManager* _asset;
    D3DFramework* _framework;

    RenderSettings& _renderSettings;

private:
    std::unique_ptr<RenderGUI> _gui;

    ProjectSettingsPanel* _projectSettings = nullptr;

    std::vector<std::unique_ptr<IEditorTool>> _tools;
    IEditorTool* _activeTool = nullptr;

private:
    float _cameraMoveSpeed = 10.0f;
    float _cameraRotateSpeed = 0.15f;
};

#endif // !EDITOR_HPP
