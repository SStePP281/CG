#ifndef DISPLAY_SETTINGS_SECTION_HPP
#define DISPLAY_SETTINGS_SECTION_HPP

#include "ProjectSettingsPanel.h"

class DisplaySettingsSection : public ISettingsSection
{
public:
	explicit DisplaySettingsSection(DisplaySettings& s) : _s(s) {}
	const char* Name() const override { return "Display"; }

	void Draw() override
	{
		ImGui::Checkbox("VSync", &_s.VSync);
		ImGui::Checkbox("Fullscreen", &_s.Fullscreen);
		ImGui::Text("Window: %u x %u", _s.WindowSize.Width, _s.WindowSize.Height);
	}

private:
	DisplaySettings& _s;
};

#endif // !DISPLAY_SETTINGS_SECTION_HPP
