#ifndef I_EDITOR_TOOL_HPP
#define I_EDITOR_TOOL_HPP

#include "GameTimer.h"
#include "Input.h"
#include "GraphicsDevice.h"
#include "D3DFramework.h"
#include "IPanel.h"

struct EditorFrameContext
{
    GraphicsDevice& device;
    D3DFramework& framework;
    Window& window;
};

class IEditorTool
{
public:
    virtual ~IEditorTool() = default;

    virtual const char* Name() const = 0;
    virtual IPanel* Panel() { return nullptr; }

    virtual void OnActivate() {}
    virtual void OnDeactivate() {}

    virtual void Update(const GameTimer& gt) {}

    virtual void PreDraw(const GameTimer& gt, const EditorFrameContext& ctx) {}
    virtual void DrawOverlay(const EditorFrameContext& ctx) {}
};

#endif // !I_EDITOR_TOOL_HPP