#ifndef RENDER_GUI_HPP
#define RENDER_GUI_HPP

#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"
#include "DescriptorHeapAllocator.h"

class RenderGUI
{
public:
    RenderGUI() = default;
    ~RenderGUI() { Shutdown(); }

    RenderGUI(const RenderGUI&) = delete;
    RenderGUI& operator=(const RenderGUI&) = delete;

    void Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue, DescriptorAllocator* srvAlloc, DXGI_FORMAT rtvFormat, UINT frameCount);

    void BeginFrame();

    void Render(ID3D12GraphicsCommandList* commandList);

    void Shutdown();

    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
    bool _initialized = false;
    bool _contextCreated = false;
    bool _win32Initialized = false;
    bool _dx12Initialized = false;

private:
    static void AllocateDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);
    static void FreeDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

    DescriptorAllocator* _srvAlloc = nullptr;
    std::vector<DescriptorRange> _imguiSrvs;
};

#endif // !RENDER_GUI_HPP