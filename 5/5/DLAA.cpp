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

void DLAA::InitializeFeature(ID3D12GraphicsCommandList* cmdList)
{
    if (!_ngxManager->IsInitialized())
    {
        OutputDebugStringA("[DLAA] NGX is not initialized.");
        return;
    }

    if (_featureHandle != nullptr)
    {
        OutputDebugStringA("[DLAA] Feature already exists.");
        return;
    }

    NVSDK_NGX_DLSS_Create_Params createParams{};

    createParams.Feature.InWidth = _render.Width;
    createParams.Feature.InHeight = _render.Height;

    createParams.Feature.InTargetWidth = _display.Width;
    createParams.Feature.InTargetHeight = _display.Height;

    createParams.Feature.InPerfQualityValue = _quality;
    createParams.InFeatureCreateFlags =
        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes
        | NVSDK_NGX_DLSS_Feature_Flags_IsHDR
        | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

    createParams.InEnableOutputSubrects = false;

    NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSS_EXT(cmdList, 1, 1, &_featureHandle, _ngxManager->GetParameters(), &createParams);

    if (NVSDK_NGX_FAILED(result))
    {
        _featureHandle = nullptr;

        std::string msg = "[DLAA] Create failed: " + std::to_string((int)result) + "\n";
        OutputDebugStringA(msg.c_str());
        return;
    }

    OutputDebugStringA("[DLAA] DLSS feature created successfully.\n");
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

Resolution DLAA::ComputeRenderResolution(const RenderSettings& s) const
{
    if (s.Preset == UpscalePreset::DLAA)
    {
        return s.Display;
    }
    else if (s.Preset == UpscalePreset::Custom)
    {
        return s.Display * s.RenderScale;
    }
    else
    {
        NVSDK_NGX_Parameter* params = _ngxManager->GetParameters();
        if (!params) { return s.Display; }

        unsigned int optW = 0, optH = 0, maxW = 0, maxH = 0, minW = 0, minH = 0;
        float sharpness = 0.0f;

        // Только из аргумента s: поля _display/_quality обновятся позже, в OnResize
        NVSDK_NGX_Result r = NGX_DLSS_GET_OPTIMAL_SETTINGS(params, s.Display.Width, s.Display.Height, ToNGXQuality(s.Preset), &optW, &optH, &maxW, &maxH, &minW, &minH, &sharpness);

        if (NVSDK_NGX_FAILED(r) || optW == 0 || optH == 0)
        {
            return s.Display;
        }

        return { optW, optH };
    }
}

void DLAA::OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, const RenderSettings& s)
{
    _render = s.Render;
    _display = s.Display;
    _quality = ToNGXQuality(s.Preset);

    if (_featureHandle) { NVSDK_NGX_D3D12_ReleaseFeature(_featureHandle); _featureHandle = nullptr; }

    CreateResources(device);
    CreateSRVAndRTV(device);

    _frameCount = 0;
    _resetHistory = true;
    _needCreateFeature = true;
}

void DLAA::Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in)
{
    if (_needCreateFeature)
    {
        InitializeFeature(cmdList);
        _needCreateFeature = false;
    }

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

    evalParams.InRenderSubrectDimensions.Width = _render.Width;
    evalParams.InRenderSubrectDimensions.Height = _render.Height;

    evalParams.InReset = _resetHistory ? 1 : 0;
    _resetHistory = false;

    evalParams.InMVScaleX = (float)_render.Width;
    evalParams.InMVScaleY = (float)_render.Height;

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
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, _display.Width, _display.Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
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

