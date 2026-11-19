#include "EditorViewport.h"
#include "MathHelper.h"
#include "imgui.h"

EditorViewport::EditorViewport(Input& input, GraphicsDevice& device, World& world, Renderer& renderer) :
	_input(&input), _world(&world), _renderer(&renderer), _bufferVisualizer(device)
{
}

void EditorViewport::Update(const GameTimer& gt)
{
	UpdateCamera(gt);
	UpdateViewModeHotkeys();

	if (_viewFlags.Mode == ViewMode::BufferVisualization) { _bufferVisualizer.DrawLabels(_viewFlags.Buffer); }

	// Before renderer->Update, so the pass constants of this frame already see the mode.
	_renderer->SetViewFlags(_viewFlags);
}

void EditorViewport::Draw()
{
	if (_viewFlags.Mode == ViewMode::BufferVisualization) { _bufferVisualizer.Draw(*_renderer, _viewFlags.Buffer); }
}

void EditorViewport::DrawMenu()
{
	if (!ImGui::BeginMenu("View Mode")) { return; }

	// Radio-style: exactly one mode is active, like UE.
	static const char* hotkeys[] = { "Alt+4", "Alt+3", "Alt+2", "Alt+6" };   // same order as ViewMode
	static_assert(_countof(hotkeys) == (size_t)ViewMode::BufferVisualization, "a hotkey label for every view mode in the list");
	for (UINT i = 0; i < (UINT)ViewMode::BufferVisualization; i++)
	{
		if (ImGui::MenuItem(ToString((ViewMode)i), hotkeys[i], _viewFlags.Mode == (ViewMode)i)) { _viewFlags.Mode = (ViewMode)i; }
	}

	if (ImGui::BeginMenu("Buffer Visualization"))
	{
		for (UINT i = 0; i < (UINT)DebugBuffer::Count; i++)
		{
			bool selected = _viewFlags.Mode == ViewMode::BufferVisualization && _viewFlags.Buffer == (DebugBuffer)i;
			if (ImGui::MenuItem(ToString((DebugBuffer)i), nullptr, selected))
			{
				_viewFlags.Mode = ViewMode::BufferVisualization;
				_viewFlags.Buffer = (DebugBuffer)i;
			}

			if (i == (UINT)DebugBuffer::Overview) { ImGui::Separator(); }
		}
		ImGui::EndMenu();
	}

	ImGui::EndMenu();
}

void EditorViewport::UpdateViewModeHotkeys()
{
	if (_input->KeyboardCaptured || !_input->IsDown(VK_MENU)) { return; }

	// Same keys as UE: Alt+2 Wireframe, Alt+3 Unlit, Alt+4 Lit, Alt+6 Lighting Only.
	struct { int Key; ViewMode Mode; } keys[] =
	{
		{ '2', ViewMode::Wireframe }, { '3', ViewMode::Unlit }, { '4', ViewMode::Lit },
		{ '6', ViewMode::LightingOnly },
	};

	for (const auto& k : keys)
	{
		if (_input->WasPressed(k.Key)) { _viewFlags.Mode = k.Mode; }
	}
}

void EditorViewport::UpdateCamera(const GameTimer& gt)
{
	Camera& cam = _world->MainCamera();

	// Wheel without modifiers: fly speed. Ctrl/Shift + wheel belongs to tools (they consume it first).
	float notches = _input->Wheel();
	if (notches != 0.0f && !_input->MouseCaptured && !_input->IsDown(VK_CONTROL) && !_input->IsDown(VK_SHIFT))
	{
		_cameraMoveSpeed *= powf(1.15f, notches);
		_cameraMoveSpeed = MathHelper::Clamp(_cameraMoveSpeed, 0.5f, 5000.0f);
	}

	// Right mouse button: look around.
	if (!_input->MouseCaptured && _input->IsDown(VK_RBUTTON))
	{
		XMFLOAT2 d = _input->MouseDelta();

		cam.Yaw += XMConvertToRadians(_cameraRotateSpeed * d.x);
		cam.Pitch -= XMConvertToRadians(_cameraRotateSpeed * d.y);
		cam.Pitch = MathHelper::Clamp(cam.Pitch, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);
	}

	// WASD: fly.
	float speed = _cameraMoveSpeed * gt.DeltaTime();

	XMVECTOR pos = XMLoadFloat3(&cam.Position);
	XMVECTOR forward = cam.Forward();
	XMVECTOR right = cam.Right();

	if (_input->IsDown('W')) { pos += speed * forward; }
	if (_input->IsDown('S')) { pos -= speed * forward; }
	if (_input->IsDown('A')) { pos -= speed * right; }
	if (_input->IsDown('D')) { pos += speed * right; }

	XMStoreFloat3(&cam.Position, pos);
}
