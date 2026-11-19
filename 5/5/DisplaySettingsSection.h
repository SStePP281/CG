#ifndef DISPLAY_SETTINGS_SECTION_HPP
#define DISPLAY_SETTINGS_SECTION_HPP

#include <cstdio>
#include <vector>

#include "ProjectSettingsPanel.h"

class DisplaySettingsSection : public ISettingsSection
{
public:
	DisplaySettingsSection(DisplaySettings& s, std::vector<Resolution> resolutions) : _s(s), _resolutions(std::move(resolutions)) {}
	const char* Name() const override { return "Display"; }

	void Draw() override
	{
		const char* modes[] = { "Windowed", "Fullscreen" };
		int mode = _s.Fullscreen ? 1 : 0;
		if (ImGui::Combo("Display Mode", &mode, modes, 2)) { _s.Fullscreen = (mode == 1); }

		char current[32];
		sprintf_s(current, "%u x %u", _s.WindowSize.Width, _s.WindowSize.Height);

		if (ImGui::BeginCombo("Resolution", current))
		{
			for (const Resolution& r : _resolutions)
			{
				char label[32];
				sprintf_s(label, "%u x %u", r.Width, r.Height);
				if (ImGui::Selectable(label, r == _s.WindowSize)) { _s.WindowSize = r; }
			}
			ImGui::EndCombo();
		}

		ImGui::Checkbox("VSync", &_s.VSync);
	}

private:
	DisplaySettings& _s;
	std::vector<Resolution> _resolutions;
};

#endif // !DISPLAY_SETTINGS_SECTION_HPP
