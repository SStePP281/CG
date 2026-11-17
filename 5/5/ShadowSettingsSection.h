#ifndef SHADOW_SETTINGS_SECTION_HPP
#define SHADOW_SETTINGS_SECTION_HPP

#include "ProjectSettingsPanel.h"

class ShadowSettingsSection : public ISettingsSection
{
public:
	explicit ShadowSettingsSection(ShadowSettings& s) : _s(s) {}
	const char* Name() const override { return "Shadows"; }

	void Draw() override
	{
		ImGui::Checkbox("Enable Shadows", &_s.EnableShadow);
		ImGui::Checkbox("Terrain Shadows", &_s.EnableTerrainShadow);

		static const UINT sizes[] = { 1024, 2048, 4096, 8192 };
		static const char* names[] = { "1024", "2048", "4096", "8192" };
		int cur = 0;
		for (int i = 0; i < 4; i++) { if (sizes[i] == _s.ShadowTextureSize) { cur = i; } }
		if (ImGui::Combo("Shadow Map Size", &cur, names, 4)) { _s.ShadowTextureSize = sizes[cur]; }
	}

private:
	ShadowSettings& _s;
};

#endif // !SHADOW_SETTINGS_SECTION_HPP
