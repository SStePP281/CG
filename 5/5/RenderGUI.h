#ifndef RENDER_GUI_HPP
#define RENDER_GUI_HPP

#include <Windows.h>
#include <d3d12.h>
#include <dxgi.h>

#include <vector>

#include "imgui.h"
#include "backends/imgui_impl_dx12.h"

class RenderGUI
{
public:
    RenderGUI() = default;
    ~RenderGUI() { Shutdown(); }

    RenderGUI(const RenderGUI&) = delete;
    RenderGUI& operator=(const RenderGUI&) = delete;

    bool Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue, ID3D12DescriptorHeap* srvHeap, DXGI_FORMAT rtvFormat, UINT frameCount, UINT firstSlot, UINT srvDescSize);

    void BeginFrame();

    void Render(ID3D12GraphicsCommandList* commandList);

    void Shutdown();

    bool HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    UINT GetSlotCount() const { return DESCRIPTOR_COUNT; }

private:
    bool Allocate(D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE& gpuHandle);

    void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle);

    static void AllocateDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle);

    static void FreeDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle);

private:
    ID3D12DescriptorHeap* _srvHeap = nullptr;
    UINT _firstSlot = 0;
    UINT _srvDescSize = 0;

    std::vector<UINT> _freeSlots;

    bool _initialized = false;
    bool _contextCreated = false;
    bool _win32Initialized = false;
    bool _dx12Initialized = false;

private:
    static constexpr UINT DESCRIPTOR_COUNT = 1;
};

#endif // !RENDER_GUI_HPP