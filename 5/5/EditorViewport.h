#ifndef EDITOR_VIEWPORT_HPP
#define EDITOR_VIEWPORT_HPP

#include "GameTimer.h"
#include "Input.h"
#include "GraphicsDevice.h"
#include "World.h"
#include "Renderer.h"
#include "RenderView.h"
#include "BufferVisualizer.h"

// Everything about how and from where the editor looks at the scene (like UE's FEditorViewportClient):
// the fly camera, the view mode and the buffer visualization.
// Works alongside any active tool: you can paint terrain in Wireframe or Lighting Only.
class EditorViewport
{
public:
	EditorViewport(Input& input, GraphicsDevice& device, World& world, Renderer& renderer);

	// After the active tool (it may consume the mouse wheel) and before renderer->Update (it reads the view flags).
	void Update(const GameTimer& gt);

	// With the back buffer bound, before the tool overlay and ImGui.
	void Draw();

	// "View Mode" entry of the main menu bar.
	void DrawMenu();

private:
	void UpdateCamera(const GameTimer& gt);
	void UpdateViewModeHotkeys();

private:
	Input* _input;
	World* _world;
	Renderer* _renderer;

	ViewFlags _viewFlags;
	BufferVisualizer _bufferVisualizer;

	float _cameraMoveSpeed = 10.0f;
	float _cameraRotateSpeed = 0.15f;
};

#endif // !EDITOR_VIEWPORT_HPP
