#ifndef TERRAIN_PAINT_PANEL_HPP
#define TERRAIN_PAINT_PANEL_HPP

#include <minwindef.h>
#include "IPanel.h"

struct BrushSettings
{
    static constexpr float RADIUS_MIN = 1.0f, RADIUS_MAX = 200.0f;
    static constexpr float STRENGTH_MIN = 0.05f, STRENGTH_MAX = 10.0f;

    enum class Mode { Draw, Erase };

    Mode  BrushMode = Mode::Draw;
    UINT  Layer = 0;
    float Radius = 20.0f;
    float Strength = 1.0f;
};

class TerrainPaintPanel : public IPanel
{
public:
    TerrainPaintPanel(BrushSettings& s, UINT layerCount) : _s(s), _layerCount(layerCount) { Open = false; }

    const char* Name() const override { return "Terrain Paint"; }
    bool ShowInMenu() const override { return false; }

    void Draw() override
    {
        int mode = (int)_s.BrushMode;
        ImGui::RadioButton("Draw", &mode, 0); ImGui::SameLine();
        ImGui::RadioButton("Erase", &mode, 1);
        _s.BrushMode = (BrushSettings::Mode)mode;

        int layer = (int)_s.Layer;
        ImGui::SliderInt("Layer", &layer, 0, (int)_layerCount - 1);
        _s.Layer = (UINT)layer;

        ImGui::SliderFloat("Radius", &_s.Radius, BrushSettings::RADIUS_MIN, BrushSettings::RADIUS_MAX, "%.1f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Strength", &_s.Strength, BrushSettings::STRENGTH_MIN, BrushSettings::STRENGTH_MAX, "%.2f", ImGuiSliderFlags_Logarithmic);

        ImGui::TextDisabled("Ctrl+Wheel: radius, Shift+Wheel: strength");
    }

private:
    BrushSettings& _s;
    UINT _layerCount;
};

#endif // !TERRAIN_PAINT_PANEL_HPP
