#include "Editor.h"
#include "InfoPanel.h"
#include "TerrainTool.h"

#include "DisplaySettingsSection.h"
#include "ShadowSettingsSection.h"
#include "AASettingsSection.h"


Editor::Editor(Window& window, Input& input, GraphicsDevice& device, World& world, AssetManager& asset, Renderer& renderer, RenderSettings& renderSettings) :
	_window(&window), _input(&input), _device(&device), _world(&world), _asset(&asset), _renderer(&renderer), _renderSettings(renderSettings)
{
	_gui = std::make_unique<RenderGUI>(_window->Handle(), *_device, GraphicsDevice::FRAME_COUNT);

	_window->MessageHook = [this](HWND h, UINT m, WPARAM w, LPARAM l)
		{
			return _gui && _gui->HandleMessage(h, m, w, l);
		};

	{
		_gui->AddPanel<InfoPanel>(_renderer->GetFrameStats(), _renderer->GetFrameHistory());
	}

	{
		_projectSettings = &_gui->AddPanel<ProjectSettingsPanel>();
		_projectSettings->AddSection<DisplaySettingsSection>(_renderSettings.Display, _device->SupportedResolutions());
		_projectSettings->AddSection<ShadowSettingsSection>(_renderSettings.Shadow);
		_projectSettings->AddSection<AASettingsSection>(_renderSettings.AA);
	}

	{
		if (TerrainData* terrain = _asset->GetTerrain(_world->Terrain())) { AddTool<TerrainTool>(*terrain, *_device, *_input, *_gui); }
	}

	_viewport = std::make_unique<EditorViewport>(*_input, *_device, *_world, *_renderer);
}

void Editor::Update(GameTimer& gt)
{
	_gui->NewFrame();

	_input->MouseCaptured = ImGui::GetIO().WantCaptureMouse;
	_input->KeyboardCaptured = ImGui::GetIO().WantCaptureKeyboard;

	DrawToolbar();

	if (_activeTool && _activeTool->Panel() && !_activeTool->Panel()->Open) { SetActiveTool(nullptr); }

	if (_activeTool) { _activeTool->Update(gt); }

	_viewport->Update(gt);
}

void Editor::PreDraw(GameTimer& gt)
{
	if (_activeTool)
	{
		_activeTool->PreDraw(gt, { *_device, *_renderer, *_window });
	}
}

void Editor::Draw()
{
	ID3D12GraphicsCommandList* l = _device->CommandList();

	auto backBuffer = _device->BackBufferRtv().Cpu();
	l->OMSetRenderTargets(1, &backBuffer, TRUE, nullptr);

	_viewport->Draw();

	if (_activeTool) { _activeTool->DrawOverlay({ *_device, *_renderer, *_window }); }

	_gui->Render(l);
}

void Editor::DrawToolbar()
{
	if (!ImGui::BeginMainMenuBar()) { return; }

	if (ImGui::BeginMenu("Edit"))
	{
		ImGui::MenuItem("Project Settings", nullptr, &_projectSettings->Open);
		ImGui::EndMenu();
	}

	_viewport->DrawMenu();

	if (ImGui::BeginMenu("Tools"))
	{
		for (auto& t : _tools)
		{
			bool active = (_activeTool == t.get());
			if (ImGui::MenuItem(t->Name(), nullptr, active)) { SetActiveTool(t.get()); }
		}
		ImGui::EndMenu();
	}

	ImGui::EndMainMenuBar();
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
