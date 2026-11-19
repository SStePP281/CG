#ifndef EDITOR_HPP
#define EDITOR_HPP

#include "Window.h"
#include "Input.h"
#include "GraphicsDevice.h"
#include "World.h"
#include "AssetManager.h"
#include "Renderer.h"
#include "RenderGUI.h"
#include "IEditorTool.h"
#include "ProjectSettingsPanel.h"
#include "EditorViewport.h"

class Editor
{
public:
    Editor(Window& window, Input& input, GraphicsDevice& device, World& world, AssetManager& asset, Renderer& renderer, RenderSettings& renderSettings);

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
    Window* _window;
    Input* _input;
    GraphicsDevice* _device;
    World* _world;
    AssetManager* _asset;
    Renderer* _renderer;

    RenderSettings& _renderSettings;

private:
    std::unique_ptr<RenderGUI> _gui;

    ProjectSettingsPanel* _projectSettings = nullptr;

    std::vector<std::unique_ptr<IEditorTool>> _tools;
    IEditorTool* _activeTool = nullptr;

    std::unique_ptr<EditorViewport> _viewport;
};

#endif // !EDITOR_HPP
