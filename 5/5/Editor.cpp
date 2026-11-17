#include "Editor.h"
#include "TerrainPaintTool.h"

Editor::Editor(Window& window, Input& input, GraphicsDevice& device, World& world, AssetManager& asset, D3DFramework& framework, RenderSettings& renderSettings) :
	_window(&window), _input(&input), _device(&device), _world(&world), _asset(&asset), _framework(&framework), _renderSettings(renderSettings)
{
	_gui = std::make_unique<RenderGUI>(_window->Handle(), *_device, GraphicsDevice::FRAME_COUNT);
	_window->MessageHook = [this](HWND h, UINT m, WPARAM w, LPARAM l)
		{
			return _gui && _gui->HandleMessage(h, m, w, l);
		};

	_gui->AddPanel<InfoPanel>(_framework->GetFrameStats(), _framework->GetFrameHistory());
	_gui->AddTool<TerrainPaintTool>(_framework->GetTerrain(), *_device, *_input, *_gui);

}

void Editor::Update(GameTimer& gt)
{
	_gui->NewFrame();

	DrawToolbar();

	if (_activeTool && _activeTool->Panel() && !_activeTool->Panel()->Open) { SetActiveTool(nullptr); }

	if (_activeTool) { _activeTool->Update(gt); }

	UpdateCameraTransform(gt);
}

void Editor::PreDraw(GameTimer& gt)
{
	if (_activeTool)
	{
		_activeTool->PreDraw(gt, { *_device, *_framework, *_window });
	}
}

void Editor::Draw()
{
	ID3D12GraphicsCommandList* l = _device->CommandList();

	auto backBuffer = _device->BackBufferRtv().Cpu();
	l->OMSetRenderTargets(1, &backBuffer, TRUE, nullptr);

	if (_activeTool) { _activeTool->DrawOverlay({ *_device, *_framework, *_window }); }

	_gui->Render(l);
}

void Editor::DrawToolbar()
{
	ImGui::Begin("Tools");

	for (auto& t : _tools)
	{
		bool active = (_activeTool == t.get());
		if (active) { ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive)); }

		if (ImGui::Button(t->Name())) { SetActiveTool(t.get()); }

		if (active) { ImGui::PopStyleColor(); }
		ImGui::SameLine();
	}
	ImGui::NewLine();

	ImGui::End();
}

void Editor::SetActiveTool(IEditorTool* tool)
{
	if (_activeTool == tool) { tool = nullptr; }

	if (_activeTool)
	{
		_activeTool->OnDeactivate();
		if (IPanel* p = _activeTool->Panel()) { p->Open = false; }
	}

	_activeTool = tool;

	if (_activeTool)
	{
		_activeTool->OnActivate();
		if (IPanel* p = _activeTool->Panel()) { p->Open = true; }
	}
}

void Editor::UpdateCameraTransform(GameTimer& gt)
{
	Camera& cam = _world->MainCamera();

	{
		float notches = _input->Wheel();

		if (notches != 0.0f && !_input->MouseCaptured)
		{
			bool ctrl = _input->IsDown(VK_CONTROL);
			bool shift = _input->IsDown(VK_SHIFT);

			if (!ctrl && !shift)
			{
				_cameraMoveSpeed *= powf(1.15f, notches);
				_cameraMoveSpeed = MathHelper::Clamp(_cameraMoveSpeed, 0.5f, 5000.0f);
			}
		}
	}

	{
		if (/*!_paintMode && */!_input->MouseCaptured && _input->IsDown(VK_RBUTTON))
		{
			XMFLOAT2 d = _input->MouseDelta();

			cam.Yaw += XMConvertToRadians(_cameraRotateSpeed * d.x);
			cam.Pitch -= XMConvertToRadians(_cameraRotateSpeed * d.y);
			cam.Pitch = MathHelper::Clamp(cam.Pitch, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);
		}
	}

	{
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
}
