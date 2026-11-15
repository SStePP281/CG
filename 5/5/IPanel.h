#ifndef I_PANEL_HPP
#define I_PANEL_HPP

#include "imgui.h"

class IPanel
{
public:
    virtual ~IPanel() = default;

    virtual const char* Name() const = 0;
    virtual void Draw() = 0;

    virtual ImGuiWindowFlags Flags() const { return 0; }
    virtual void BeforeBegin() {}
    virtual bool ShowInMenu() const { return true; }

    bool Open = true;
};

#endif // !I_PANEL_HPP