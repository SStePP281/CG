#ifndef POST_PROCESS_HPP
#define POST_PROCESS_HPP

#include <d3d12.h>
#include <d3dx12.h>

#include <wrl/client.h>

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <algorithm>

#include "ThrowIfFaild.h"
#include "D3DUtil.h"

using namespace Microsoft::WRL;

constexpr DXGI_FORMAT HDR_FORMAT = DXGI_FORMAT_R16G16B16A16_FLOAT;

enum class PostProcessStage
{
    BeforeTonemapping = 0,
    Tonemapping = 1,
    AfterTonemapping = 2
};

struct PassDesc
{
    std::string Name;

    std::string ShaderPath;
    std::string VS = "VS";
    std::string PS = "PS";

    bool HasConstantBuffer = false;
    UINT ConstantBufferSize = 0;
    const void* ConstantBufferData = nullptr;

    DXGI_FORMAT OutputFormat = HDR_FORMAT;
    bool UseDepth = false;
    int Width = 0;
    int Height = 0;

    PostProcessStage Stage = PostProcessStage::BeforeTonemapping;
    int Priority = 0;

    std::vector<UINT> InputSrvSlots;

    std::vector<UINT> ExtraInputSrvSlots;
};

class PostProcessPass
{
public:
    PostProcessPass(ID3D12Device* device, const PassDesc& desc, ID3D12DescriptorHeap* srvHeap, UINT& lastSlot, UINT srvDescSize);
    void Build(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvDescSize);
    void Execute(ID3D12GraphicsCommandList* cmdList, ID3D12DescriptorHeap* srvHeap, UINT srvDescSize, D3D12_CPU_DESCRIPTOR_HANDLE rtv);

    void OnResize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvDescSize, int width, int height);

    PassDesc Desc;
    UINT OutputSrvSlot = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE OutputRTV = {};
    D3D12_GPU_DESCRIPTOR_HANDLE OutputSRV = {};

    ComPtr<ID3D12Resource> OutputBuffer;
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PSO;
    ComPtr<ID3D12DescriptorHeap> RTVHeap;

private:

    void CreateResources(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT& lastSlot, UINT srvDescSize);
    void CreateOutputBuffer(ID3D12Device* device);
    void BuildRootSignature(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvDescSize);
    void BuildPSO(ID3D12Device* device);

    ComPtr<ID3D12Resource> _constantBuffer;
};

class PostProcessChain
{
public:
    void InitHDRBuffer(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT& lastSlot, UINT srvDescSize, int width, int height);

    UINT ReservePass(ID3D12Device* device, const PassDesc& desc, ID3D12DescriptorHeap* srvHeap, UINT& lastSlot, UINT srvDescSize);
    void CommitAll(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvDescSize);
    void ExecuteAll(ID3D12GraphicsCommandList* cmdList, ID3D12DescriptorHeap* srvHeap, UINT srvDescSize, D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV);

    PostProcessPass* GetPass(const std::string& name);
    PostProcessPass* First() { return _passes[0].get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE GetLightPassRTV() const { return _hdrRTV; }
    ID3D12Resource* GetHDRBuffer() const { return _hdrBuffer.Get(); }

    void OnResize(ID3D12Device* device, ID3D12DescriptorHeap* srvHeap, UINT srvDescSize, int width, int height);

private:
    void Sort();

    std::vector<std::unique_ptr<PostProcessPass>> _passes;

    ComPtr<ID3D12Resource> _hdrBuffer;
    ComPtr<ID3D12DescriptorHeap> _hdrRTVHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE _hdrRTV = {};
    UINT _hdrSrvSlot = 0;
};

#endif // !POST_PROCESS_HPP