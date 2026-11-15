#ifndef RENDER_GUI_HPP
#define RENDER_GUI_HPP

#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include <vector>
#include <memory>

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "GraphicsDevice.h"
#include "IPanel.h"

class RenderGUI
{
public:
    RenderGUI(HWND hwnd, GraphicsDevice& device, UINT frameCount);
    ~RenderGUI() { Shutdown(); }

    RenderGUI(const RenderGUI&) = delete;
    RenderGUI& operator=(const RenderGUI&) = delete;

    template<class T, class... Args>
    T& AddPanel(Args&&... args)
    {
        auto panel = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *panel;
        _panels.push_back(std::move(panel));
        return ref;
    }

    void NewFrame();
    void Render(ID3D12GraphicsCommandList* commandList);
    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void Shutdown();

    bool Visible = true;

private:
    void DrawMenuBar();
    void DrawPanels();

    static void AllocateDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);
    static void FreeDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

private:
    bool _initialized = false;
    bool _contextCreated = false;
    bool _win32Initialized = false;
    bool _dx12Initialized = false;

    DescriptorAllocator& _srvAlloc;
    std::vector<DescriptorRange> _imguiSrvs;

    std::vector<std::unique_ptr<IPanel>> _panels;
};

#endif // !RENDER_GUI_HPP