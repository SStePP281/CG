#ifndef ATMOSPHERE_PANEL_HPP
#define ATMOSPHERE_PANEL_HPP

#include "IPanel.h"
#include "AtmosphereData.h"

class AtmospherePanel : public IPanel
{
public:
    AtmospherePanel(AtmosphereData& data) : _data(data) { Open = false; }

    const char* Name() const override { return "Atmosphere"; }

    void Draw() override
    {
        ImGui::SeparatorText("Sun");
        ImGui::SliderFloat("Azimuth", &_data.SunAzimuth, 0.0f, 360.0f, "%.0f deg");
        ImGui::SliderFloat("Elevation", &_data.SunElevation, -10.0f, 90.0f, "%.1f deg");
        ImGui::DragFloat("Sky intensity", & _data.SunIntensity, 0.1f, 0.0f, 100.0f);
        ImGui::DragFloat("Light scale intensity", &_data.SunLightScale, 0.1f, 0.0f, 10.0f);
        ImGui::ColorEdit3("Color", &_data.SunColor.x);

        ImGui::SeparatorText("Rayleigh");
        ImGui::SliderFloat("Scale##rayleigh", &_data.RayleighScale, 0.0f, 10.0f);
        ImGui::DragFloat("Height##rayleigh", &_data.RayleighHeight, 10.0f, 100.0f, 20000.0f, "%.0f m");
    
        ImGui::SeparatorText("Mie");
        float mie = _data.MieBeta * 1e6f;
        if (ImGui::SliderFloat("Beta##mie", &mie, 1.0f, 10000.0f, "%.1f e-6", ImGuiSliderFlags_Logarithmic))
        {
            _data.MieBeta = mie * 1e-6f;
        }
        ImGui::SliderFloat("Scale##mie", &_data.MieScale, 0.0f, 10.0f);
        ImGui::SliderFloat("g##mie", &_data.MieG, 0.0f, 0.99f);
        ImGui::DragFloat("Height##mie", &_data.MieHeight, 10.0f, 100.0f, 10000.0f, "%.0f m");

        ImGui::SeparatorText("Scene");
        ImGui::DragFloat("Meters per unit", &_data.MetersPerUnit, 0.1f, 0.01f, 1000.0f);
        ImGui::Checkbox("Affect objects", &_data.AffectObjects);

        ImGui::SeparatorText("IBL");
        ImGui::SliderFloat("IBL Intensity", &_data.IBLIntensity, 0.0f, 10.0f, "%.1f");

        if (ImGui::Button("Reset")) { _data.Clear(); }
    }

private:
    AtmosphereData& _data;
};

#endif // !ATMOSPHERE_PANEL_HPP