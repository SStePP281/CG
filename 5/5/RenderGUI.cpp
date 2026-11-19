#include "RenderGUI.h"

#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include "d3dx12.h"
#include <algorithm>
#include <iostream>
#include <Windows.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

RenderGUI::RenderGUI(HWND hwnd, GraphicsDevice& device, UINT frameCount) : _device(device), _srvAlloc(device.Srv())
{
    if (_initialized) { return; }

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    _contextCreated = true;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(hwnd))
    {
        std::cout << "[GUI] ImGui_ImplWin32_Init failed, GUI disabled" << std::endl;
        Shutdown();
        return;
    }
    _win32Initialized = true;

    ImGui_ImplDX12_InitInfo initInfo = {};

    initInfo.Device = device.Device();
    initInfo.CommandQueue = device.Queue();
    initInfo.NumFramesInFlight = frameCount;
    initInfo.RTVFormat = device.BackBufferFormat();
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = _srvAlloc.Heap();
    initInfo.UserData = this;
    initInfo.SrvDescriptorAllocFn = AllocateDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeDescriptor;

    if (!ImGui_ImplDX12_Init(&initInfo))
    {
        std::cout << "[GUI] ImGui_ImplDX12_Init failed, GUI disabled" << std::endl;
        Shutdown();
        return;
    }

    _dx12Initialized = true;
    _initialized = true;

    std::cout << "[GUI] ImGui initialized" << std::endl;
}

void RenderGUI::NewFrame()
{
    if (!_initialized) { return; }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();

    // ImGui lays out and takes the mouse in window pixels; the back buffer can be smaller (fullscreen at a lower resolution).
    // FramebufferScale maps window pixels to back buffer pixels for the DX12 backend's viewport and scissor rects.
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x > 0.0f && io.DisplaySize.y > 0.0f)
    {
        Resolution bb = _device.BackBufferSize();
        io.DisplayFramebufferScale = ImVec2(bb.Width / io.DisplaySize.x, bb.Height / io.DisplaySize.y);
    }

    ImGui::NewFrame();

    if (!Visible) { return; }

    DrawMenuBar();
    DrawPanels();
}

void RenderGUI::DrawMenuBar()
{
    if (!ImGui::BeginMainMenuBar()) { return; }

    if (ImGui::BeginMenu("View"))
    {
        for (auto& p : _panels)
        {
            if (p->ShowInMenu()) { ImGui::MenuItem(p->Name(), nullptr, &p->Open); }
        }
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void RenderGUI::DrawPanels()
{
    for (auto& p : _panels)
    {
        if (!p->Open) { continue; }

        p->BeforeBegin();

        if (ImGui::Begin(p->Name(), &p->Open, p->Flags()))
        {
            p->Draw();
        }
        ImGui::End();
    }
}

void RenderGUI::Render(ID3D12GraphicsCommandList* commandList)
{
    if (!_initialized || !commandList) { return; }

    ImGui::Render();

    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

bool RenderGUI::HandleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (!_win32Initialized) { return false; }

    return ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam ) != 0;
}

void RenderGUI::Shutdown()
{
    if (_dx12Initialized)
    {
        ImGui_ImplDX12_Shutdown();
        _dx12Initialized = false;
    }

    if (_win32Initialized)
    {
        ImGui_ImplWin32_Shutdown();
        _win32Initialized = false;
    }

    if (_contextCreated)
    {
        ImGui::DestroyContext();
        _contextCreated = false;
    }

    _initialized = false;
}

void RenderGUI::AllocateDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
{
    auto* gui = static_cast<RenderGUI*>(info->UserData);

    DescriptorRange r = gui->_srvAlloc.Allocate(1);
    gui->_imguiSrvs.push_back(r);

    *cpuHandle = r.Cpu();
    *gpuHandle = r.Gpu();
}

void RenderGUI::FreeDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
{
    RenderGUI* gui = static_cast<RenderGUI*>(info->UserData);
    auto& v = gui->_imguiSrvs;

    auto it = std::find_if(v.begin(), v.end(), [&](const DescriptorRange& r) { return r.Cpu().ptr == cpuHandle.ptr; });
    assert(it != v.end() && "ImGui freed a descriptor it never allocated");

    gui->_srvAlloc.Free(*it);
    v.erase(it);
}