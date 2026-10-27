#include "DLAA.h"
#include "ThrowIfFaild.h"
#include <iostream>
#include "nvsdk_ngx_helpers.h"
#include "nvsdk_ngx_params.h"

DLAA::DLAA(ID3D12GraphicsCommandList* cmdList, NGXManager* ngxManager, const AAInitContext& ctx) :
	_ngxManager(ngxManager), _srvHeap(ctx.srvHeap), _srvDescSize(ctx.srvDescSize), _firstSlot(ctx.firstSlot)
{
    _rtvDescriptorSize = ctx.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = 1;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(ctx.device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap)));
}

void DLAA::InitializeFeature(ID3D12GraphicsCommandList* cmdList, const AAResolution& res)
{
    if (!_ngxManager->IsInitialized())
    {
        std::cerr << "[DLAA] NGX is not initialized.\n";
        return;
    }

    if (_featureHandle != nullptr)
    {
        std::cerr << "[DLAA] Feature already exists.\n";
        return;
    }

    if (res.renderW == 0 || res.renderH == 0 || res.displayW == 0 || res.displayH == 0)
    {
        OutputDebugStringA("[DLAA] Invalid resolution.\n");
        return;
    }

    if (res.renderW != res.displayW || res.renderH != res.displayH)
    {
        OutputDebugStringA("[DLAA] Render and display resolutions must match.\n");
        return;
    }

    NVSDK_NGX_Parameter* params = nullptr;

    NVSDK_NGX_Result paramResult = NVSDK_NGX_D3D12_AllocateParameters(&params);

    if (NVSDK_NGX_FAILED(paramResult) || !params)
    {
        OutputDebugStringA("[DLAA] Failed to allocate NGX parameters.\n");
        return;
    }

    NVSDK_NGX_DLSS_Create_Params createParams{};

    createParams.Feature.InWidth = res.renderW;
    createParams.Feature.InHeight = res.renderH;

    createParams.Feature.InTargetWidth = res.displayW;
    createParams.Feature.InTargetHeight = res.displayH;

    createParams.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
    createParams.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_None;

    createParams.InEnableOutputSubrects = false;

    NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSS_EXT(cmdList, 1, 1, &_featureHandle, params, &createParams);

    NVSDK_NGX_D3D12_DestroyParameters(params);

    if (NVSDK_NGX_FAILED(result))
    {
        _featureHandle = nullptr;

        OutputDebugStringA("[DLAA] Failed to create DLSS feature.\n");
        return;
    }

    OutputDebugStringA("[DLAA] DLAA feature created successfully.\n");

    return;
}

DLAA::~DLAA()
{
    if (_featureHandle != nullptr)
    {
        const NVSDK_NGX_Result result = NVSDK_NGX_D3D12_ReleaseFeature(_featureHandle);

        if (NVSDK_NGX_FAILED(result))
        {
            std::cerr << "[DLAA] Failed to release feature: " << static_cast<int>(result) << '\n';
        }

        _featureHandle = nullptr;
    }
}

void DLAA::OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const AAResolution& res)
{
    _frameCount = 0;

    _width = res.renderW;
    _height = res.renderH;

    CreateResources(device);
    CreateSRVAndRTV(device);

    InitializeFeature(cmdList, res);
}

void DLAA::Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in)
{
    if (!_featureHandle) { return; }

    auto outputToWrite = CD3DX12_RESOURCE_BARRIER::Transition(_output.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmdList->ResourceBarrier(1, &outputToWrite);

    NVSDK_NGX_D3D12_DLSS_Eval_Params evalParams = {};

    evalParams.Feature.pInColor = in.color.resource;
    evalParams.Feature.pInOutput = _output.Get();
    evalParams.Feature.InSharpness = 0.0f;

    evalParams.pInDepth = in.depth.resource;
    evalParams.pInMotionVectors = in.velocity.resource;

    evalParams.InJitterOffsetX = _lastJitter.x;
    evalParams.InJitterOffsetY = _lastJitter.y;

    evalParams.InRenderSubrectDimensions.Width = _width;
    evalParams.InRenderSubrectDimensions.Height = _height;

    evalParams.InReset = (_frameCount == 0) ? 1 : 0;

    evalParams.InMVScaleX = 1.0f;
    evalParams.InMVScaleY = 1.0f;

    evalParams.InPreExposure = 1.0f;
    evalParams.InExposureScale = 1.0f;

    NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSS_EXT(cmdList, _featureHandle, _ngxManager->GetParameters(), &evalParams);

    auto outputToRead = CD3DX12_RESOURCE_BARRIER::Transition(_output.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

    cmdList->ResourceBarrier(1, &outputToRead);

    if (NVSDK_NGX_FAILED(result))
    {
        OutputDebugStringA("[DLAA] NGX DLSS evaluation failed.\n");
    }
}

void DLAA::CreateResources(ID3D12Device* device)
{
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, _width, _height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clear.Color[0] = 0.0f;
    clear.Color[1] = 0.0f;
    clear.Color[2] = 0.0f;
    clear.Color[3] = 0.0f;

    ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, &clear, IID_PPV_ARGS(_output.ReleaseAndGetAddressOf())));
}

void DLAA::CreateSRVAndRTV(ID3D12Device* device)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE srv = CD3DX12_CPU_DESCRIPTOR_HANDLE(_srvHeap->GetCPUDescriptorHandleForHeapStart(), _firstSlot, _srvDescSize);

    const D3D12_RESOURCE_DESC colorDesc = _output.Get()->GetDesc();

    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = colorDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = colorDesc.MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        device->CreateShaderResourceView(_output.Get(), &srvDesc, srv);
        srv.Offset(1, _srvDescSize);
    }

    {
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvStart(_rtvHeap->GetCPUDescriptorHandleForHeapStart());

        D3D12_RENDER_TARGET_VIEW_DESC colorRTVDesc = {};
        colorRTVDesc.Format = colorDesc.Format;
        colorRTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        _rtv = CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvStart, 0, _rtvDescriptorSize);
        device->CreateRenderTargetView(_output.Get(), &colorRTVDesc, _rtv);
    }
}

