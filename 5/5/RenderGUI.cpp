#include "RenderGUI.h"

#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx12.h"
#include "d3dx12.h"

#include <Windows.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

bool RenderGUI::Initialize(HWND hwnd, ID3D12Device* device, ID3D12CommandQueue* commandQueue, ID3D12DescriptorHeap* srvHeap, DXGI_FORMAT rtvFormat, UINT frameCount, UINT firstSlot, UINT srvDescSize)
{
	if (_initialized) { return true; }

	if (!hwnd || !device || !commandQueue || !srvHeap || frameCount == 0) { return false; }

    _srvHeap = srvHeap;
    _srvDescSize = srvDescSize;
    _firstSlot = firstSlot;

    _freeSlots.clear();
    for (UINT i = 0; i < DESCRIPTOR_COUNT; ++i)
    {
        _freeSlots.push_back(_firstSlot + DESCRIPTOR_COUNT - 1 - i);
    }

    IMGUI_CHECKVERSION();

    ImGui::CreateContext();
    _contextCreated = true;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui::StyleColorsDark();

    if (!ImGui_ImplWin32_Init(hwnd))
    {
        Shutdown();
        return false;
    }
    _win32Initialized = true;

    ImGui_ImplDX12_InitInfo initInfo = {};

    initInfo.Device = device;
    initInfo.CommandQueue = commandQueue;
    initInfo.NumFramesInFlight = frameCount;
    initInfo.RTVFormat = rtvFormat;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    initInfo.SrvDescriptorHeap = srvHeap;
    initInfo.UserData = this;
    initInfo.SrvDescriptorAllocFn = AllocateDescriptor;
    initInfo.SrvDescriptorFreeFn = FreeDescriptor;

    if (!ImGui_ImplDX12_Init(&initInfo))
    {
        Shutdown();
        return false;
    }

    _dx12Initialized = true;
    _initialized = true;

    return true;
}

void RenderGUI::BeginFrame()
{
    if (!_initialized) { return; }

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();
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

    _freeSlots.clear();

    _srvHeap = nullptr;
    _firstSlot = 0;
    _srvDescSize = 0;

    _initialized = false;
}

bool RenderGUI::Allocate(D3D12_CPU_DESCRIPTOR_HANDLE& cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE& gpuHandle)
{
    if (_freeSlots.empty()) { return false; }

    UINT slot = _freeSlots.back();
    _freeSlots.pop_back();

    cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(_srvHeap->GetCPUDescriptorHandleForHeapStart(), slot, _srvDescSize);

    gpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(_srvHeap->GetGPUDescriptorHandleForHeapStart(), slot, _srvDescSize);

    return true;
}

void RenderGUI::Free(D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle)
{
    const D3D12_CPU_DESCRIPTOR_HANDLE heapStart = _srvHeap->GetCPUDescriptorHandleForHeapStart();

    if (cpuHandle.ptr < heapStart.ptr) { return; }

    const SIZE_T offset = cpuHandle.ptr - heapStart.ptr;

    if (offset % _srvDescSize != 0) { return; }

    const UINT slot = static_cast<UINT>(offset / _srvDescSize);

    if (slot < _firstSlot || slot >= _firstSlot + DESCRIPTOR_COUNT) { return; }

    for (UINT freeSlot : _freeSlots)
    {
        if (freeSlot == slot) { return; }
    }

    _freeSlots.push_back(slot);
}

void RenderGUI::AllocateDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE* gpuHandle)
{
    RenderGUI* gui = static_cast<RenderGUI*>(info->UserData);

    if (!gui->Allocate(*cpuHandle, *gpuHandle))
    {
        *cpuHandle = {};
        *gpuHandle = {};
    }
}

void RenderGUI::FreeDescriptor(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle)
{
    RenderGUI* gui = static_cast<RenderGUI*>(info->UserData);

    gui->Free(cpuHandle);
}