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

#include "RenderSettings.h"
#include "DescriptorHeapAllocator.h"

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
    Resolution Res;

    PostProcessStage Stage = PostProcessStage::BeforeTonemapping;
    int Priority = 0;

    std::vector<DescriptorRange> ExtraInputSrv;
};

class PostProcessPass
{
public:
    PostProcessPass(ID3D12Device* device, const PassDesc& desc, DescriptorAllocator* srvAlloc);
    void Build(ID3D12Device* device);
    void Execute(ID3D12GraphicsCommandList* cmdList, DescriptorRange input, D3D12_CPU_DESCRIPTOR_HANDLE rtv);

    void OnResize(ID3D12Device* device, Resolution res);

    PassDesc Desc;

    D3D12_CPU_DESCRIPTOR_HANDLE OutputRTV = {};
    DescriptorRange OutputSRV = {};

    ComPtr<ID3D12Resource> OutputBuffer;
    ComPtr<ID3D12RootSignature> RootSignature;
    ComPtr<ID3D12PipelineState> PSO;
    ComPtr<ID3D12DescriptorHeap> RTVHeap;

private:

    void CreateResources(ID3D12Device* device, DescriptorAllocator* srvAlloc);
    void CreateOutputBuffer(ID3D12Device* device);
    void BuildRootSignature(ID3D12Device* device);
    void BuildPSO(ID3D12Device* device);

    ComPtr<ID3D12Resource> _constantBuffer;
};

class PostProcessChain
{
public:
    void InitHDRBuffer(ID3D12Device* device, DescriptorAllocator* srvAlloc, Resolution res);

    DescriptorRange ReservePass(ID3D12Device* device, const PassDesc& desc, DescriptorAllocator* srvAlloc);
    void CommitAll(ID3D12Device* device);
    void ExecuteAll(ID3D12GraphicsCommandList* cmdList, DescriptorRange firstInput, D3D12_CPU_DESCRIPTOR_HANDLE backBufferRTV);

    PostProcessPass* GetPass(const std::string& name);
    PostProcessPass* First() { return _passes[0].get(); }

    D3D12_CPU_DESCRIPTOR_HANDLE GetLightPassRTV() const { return _hdrRTV; }
    ID3D12Resource* GetHDRBuffer() const { return _hdrBuffer.Get(); }
    DescriptorRange GetHDRSrv() const { return _hdrSrv; }

    void OnResize(ID3D12Device* device, Resolution render, Resolution display);

private:
    void Sort();

    std::vector<std::unique_ptr<PostProcessPass>> _passes;

    ComPtr<ID3D12Resource> _hdrBuffer;
    ComPtr<ID3D12DescriptorHeap> _hdrRTVHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE _hdrRTV = {};
    DescriptorRange _hdrSrv = {};
};

#endif // !POST_PROCESS_HPP