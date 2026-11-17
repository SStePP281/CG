#ifndef TERRAIN_PAINT_PANEL_HPP
#define TERRAIN_PAINT_PANEL_HPP

#include <minwindef.h>
#include "IPanel.h"
#include "TerrainData.h"

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

class TerrainToolPanel : public IPanel
{
public:
    enum class TerrainMode
    {
        Settings,
        Paint,
        Sculpt
    };

public:
    TerrainToolPanel(TerrainData& data, BrushSettings& brush, TerrainMode& mode) : _brush(brush), _data(&data), _mode(mode) { Open = false; }

    const char* Name() const override { return "Terrain"; }
    bool ShowInMenu() const override { return false; }

    void Draw() override
    {
        if (!ImGui::BeginTabBar("##terrain")) { return; }

        if (ImGui::BeginTabItem("Settings")) 
        { 
            _mode = TerrainMode::Settings;
            DrawSettings();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Paint"))
        {
            _mode = TerrainMode::Paint;
            DrawBrush();
            DrawPaint();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Sculpt"))
        {
            _mode = TerrainMode::Sculpt;
            DrawBrush();
            DrawSclupt();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

private:
    void DrawSettings()
    {
        float scale = _data->HeightScale();
        if (ImGui::DragFloat("Height Scale", &scale, 1.0f, -2000.0f, 2000.0f)) { _data->SetHeightScale(scale); }

        float offset = _data->HeightOffset();
        if (ImGui::DragFloat("Height Offset", &offset, 1.0f, -2000.0f, 2000.0f)) { _data->SetHeightOffset(offset); }

        float size = _data->WorldSize();
        if (ImGui::DragFloat("World Size", &size, 8.0f, 64.0f, 16384.0f)) { _data->SetWorldSize(size); }

        ImGui::SeparatorText("Layers");
        for (UINT i = 0; i < _data->LayerCount(); i++)
        {
            ImGui::PushID(i);
            float tile = _data->Layers()[i].TileSize;
            if (ImGui::DragFloat("Tile Size", &tile, 0.5f, 0.5f, 4096.0f)) { _data->SetLayerTileSize(i, tile); }
            ImGui::PopID();
        }
    }

    void DrawBrush()
    {
        ImGui::SliderFloat("Radius", &_brush.Radius, BrushSettings::RADIUS_MIN, BrushSettings::RADIUS_MAX, "%.1f", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Strength", &_brush.Strength, BrushSettings::STRENGTH_MIN, BrushSettings::STRENGTH_MAX, "%.2f", ImGuiSliderFlags_Logarithmic);

        ImGui::TextDisabled("Ctrl + Wheel: radius\nShift + Wheel: strength");
    }

    void DrawPaint()
    {
        int mode = (int)_brush.BrushMode;
        ImGui::RadioButton("Draw", &mode, 0); ImGui::SameLine();
        ImGui::RadioButton("Erase", &mode, 1);
        _brush.BrushMode = (BrushSettings::Mode)mode;

        int layer = (int)_brush.Layer;
        ImGui::SliderInt("Layer", &layer, 0, (int)_data->LayerCount() - 1);
        _brush.Layer = (UINT)layer;

        ImGui::TextDisabled("Num 1-9: layer");
    }

    void DrawSclupt()
    {

    }

private:
    TerrainData* _data;
    BrushSettings& _brush;

    TerrainMode& _mode;
};

#endif // !TERRAIN_PAINT_PANEL_HPP
