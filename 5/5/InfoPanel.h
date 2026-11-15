#ifndef INFO_PANEL_HPP
#define INFO_PANEL_HPP

#include "IPanel.h"
#include "FrameStats.h"

class InfoPanel : public IPanel
{
public:
    InfoPanel(const FrameStats& stats, const FrameTimeHistory& history) : _stats(stats), _history(history) {}

    const char* Name() const override { return "Info"; }

    ImGuiWindowFlags Flags() const override
    {
        return ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
    }

    void BeforeBegin() override
    {
        const float pad = 10.0f;
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({ vp->WorkPos.x + pad, vp->WorkPos.y + pad });
        ImGui::SetNextWindowBgAlpha(0.35f);
    }

    void Draw() override
    {
        float ms = _history.Avg();
        ImGui::Text("FPS: %.0f", ms > 0.0f ? 1000.0f / ms : 0.0f);
        ImGui::Text("Frame: %.2f ms (max %.2f)", ms, _history.Max());
        ImGui::PlotLines("##frametime", _history.Data(), FrameTimeHistory::SIZE, _history.Offset(), nullptr, 0.0f, 33.3f, { 220, 40 });
        ImGui::Separator();
        ImGui::Text("Draw calls: %u (shadow %u)", _stats.DrawCalls, _stats.ShadowDraws);
        ImGui::Text("Triangles: %u", _stats.Triangles);
        ImGui::Text("Instances: %u / %u", _stats.VisibleInstances, _stats.TotalInstances);
        ImGui::Text("SRV: %u / %u", _stats.SrvUsed, _stats.SrvCapacity);
        ImGui::Text("RTV: %u / %u", _stats.RtvUsed, _stats.RtvCapacity);
    }

private:
    const FrameStats& _stats;
    const FrameTimeHistory& _history;
};

#endif // !INFO_PANEL_HPP