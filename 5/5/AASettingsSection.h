#ifndef AA_SETTINGS_SECTION_HPP
#define AA_SETTINGS_SECTION_HPP

#include "ProjectSettingsPanel.h"

class AASettingsSection : public ISettingsSection
{
public:
	explicit AASettingsSection(AASettings& s) : _s(s) {}
	const char* Name() const override { return "Anti-Aliasing"; }

	void Draw() override
	{
		const char* modes[] = { "None", "TAA", "DLSS" };
		int mode = (int)_s.Mode;
		if (ImGui::Combo("Method", &mode, modes, 3)) { _s.Mode = (AASettings::AAMode)mode; }

		if (_s.Mode != AASettings::AAMode::DLAA) { return; }

		const char* presets[] = { "DLAA", "Quality", "Balanced", "Performance", "Ultra Performance", "Custom" };
		int preset = (int)_s.Preset;
		if (ImGui::Combo("Preset", &preset, presets, 6)) { _s.Preset = (AASettings::UpscalePreset)preset; }

		if (_s.Preset == AASettings::UpscalePreset::Custom)
		{
			ImGui::SliderFloat("Render Scale", &_s.RenderScale, 0.33f, 1.0f, "%.2f");
		}
	}

private:
	AASettings& _s;
};

#endif // !AA_SETTINGS_SECTION_HPP
