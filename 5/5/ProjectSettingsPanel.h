#ifndef PROJECT_SETTINGS_PANEL
#define PROJECT_SETTINGS_PANEL

#include "IPanel.h"
#include "RenderSettings.h"
#include <vector>
#include <memory>

class ISettingsSection
{
public:
	virtual ~ISettingsSection() = default;

	virtual const char* Name() const = 0;
	virtual void Draw() = 0;
};

class ProjectSettingsPanel : public IPanel
{
public:
	ProjectSettingsPanel() { Open = false; }

	const char* Name() const override { return "Project Settings"; }
	bool ShowInMenu() const override { return false; }

	template<class T, class... Args>
	T& AddSection(Args&&... args)
	{
		auto s = std::make_unique<T>(std::forward<Args>(args)...);
		T& ref = *s;
		_sections.push_back(std::move(s));
		return ref;
	}

	void Draw() override
	{
		ImGui::BeginChild("##categories", ImVec2(160, 0), ImGuiChildFlags_Borders);
		for (int i = 0; i < (int)_sections.size(); i++)
		{
			if (ImGui::Selectable(_sections[i]->Name(), _selected == i)) { _selected = i; }
		}
		ImGui::EndChild();

		ImGui::SameLine();

		ImGui::BeginChild("##content");
		if (_selected < (int)_sections.size())
		{
			ImGui::SeparatorText(_sections[_selected]->Name());
			_sections[_selected]->Draw();
		}
		ImGui::EndChild();
	}

private:
	std::vector<std::unique_ptr<ISettingsSection>> _sections;
	int _selected = 0;
};

#endif // !PROJECT_SETTINGS_PANEL
