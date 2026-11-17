#include "TAA.h"
#include "ThrowIfFaild.h"
#include "D3DUtil.h"
#include "PathManager.h"

TAA::TAA(const AAInitContext& ctx)
{
    _srvRange = ctx.device.Srv().Allocate(2);
    _rtvRange = ctx.device.Rtv().Allocate(2);

    CreateRootsignature(ctx);
    CreatePSO(ctx);
}

void TAA::OnResize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, AASettings& aaSettings, const Resolution& display)
{
    _currentIndex = 0;
    _frameCount = 0;

    _width = display.Width;
    _height = display.Height;

    CreateResources(device);
    CreateSRVAndRTV(device);
}

void TAA::Resolve(ID3D12GraphicsCommandList* cmdList, const AAInputs& in)
{
    _currentIndex = 1 - _currentIndex;

    auto toWrite = CD3DX12_RESOURCE_BARRIER::Transition(_colorBuffer[_currentIndex].Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
    cmdList->ResourceBarrier(1, &toWrite);

    cmdList->IASetVertexBuffers(0, 0, nullptr);
    cmdList->IASetIndexBuffer(nullptr);
    cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    cmdList->SetPipelineState(_pso.Get());
    cmdList->SetGraphicsRootSignature(_rootSignature.Get());

    auto rtv = _rtvRange.Cpu(_currentIndex);
    cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    cmdList->SetGraphicsRootDescriptorTable(0, _srvRange.Gpu(1 - _currentIndex));   // t0
    cmdList->SetGraphicsRootDescriptorTable(1, in.color.srv.Gpu());	                // t1 
    cmdList->SetGraphicsRootDescriptorTable(2, in.prevVelocity.srv.Gpu());          // t2 
    cmdList->SetGraphicsRootDescriptorTable(3, in.velocity.srv.Gpu());	            // t3 
    cmdList->SetGraphicsRootDescriptorTable(4, in.depth.srv.Gpu());	                // t4

    D3D12_RECT fullRect = { 0, 0, (LONG)_width, (LONG)_height};
    cmdList->RSSetScissorRects(1, &fullRect);

    cmdList->DrawInstanced(3, 1, 0, 0);

    auto toRead = CD3DX12_RESOURCE_BARRIER::Transition(_colorBuffer[_currentIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    cmdList->ResourceBarrier(1, &toRead);
}

void TAA::CreateRootsignature(const AAInitContext& ctx)
{
    CD3DX12_DESCRIPTOR_RANGE prevColorRange;
    prevColorRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE curColorRange;
    curColorRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

    CD3DX12_DESCRIPTOR_RANGE prevVelocityRange;
    prevVelocityRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

    CD3DX12_DESCRIPTOR_RANGE curVelocityRange;
    curVelocityRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

    CD3DX12_DESCRIPTOR_RANGE depthRange;
    depthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

    CD3DX12_ROOT_PARAMETER slotRootParameter[5];
    slotRootParameter[0].InitAsDescriptorTable(1, &prevColorRange);
    slotRootParameter[1].InitAsDescriptorTable(1, &curColorRange);
    slotRootParameter[2].InitAsDescriptorTable(1, &prevVelocityRange);
    slotRootParameter[3].InitAsDescriptorTable(1, &curVelocityRange);
    slotRootParameter[4].InitAsDescriptorTable(1, &depthRange);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter, ctx.samplerCount, ctx.samplers, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if (errorBlob != nullptr)
    {
        OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }

    ThrowIfFailed(hr);

    ThrowIfFailed(ctx.device.Device()->CreateRootSignature(0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&_rootSignature)
    ));
}

void TAA::CreatePSO(const AAInitContext& ctx)
{
    CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);

    CD3DX12_DEPTH_STENCIL_DESC dsDesc(D3D12_DEFAULT);
    dsDesc.DepthEnable = FALSE;
    dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.pRootSignature = _rootSignature.Get();
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = dsDesc;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

    auto const& vs = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/TAA.hlsl", nullptr, "VS", "vs_5_1");
    auto const& ps = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/TAA.hlsl", nullptr, "PS", "ps_5_1");

    psoDesc.VS =
    {
        reinterpret_cast<BYTE*>(vs->GetBufferPointer()), vs->GetBufferSize()
    };

    psoDesc.PS =
    {
        reinterpret_cast<BYTE*>(ps->GetBufferPointer()), ps->GetBufferSize()
    };

    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    psoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

    ThrowIfFailed(ctx.device.Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_pso)));
}

void TAA::CreateResources(ID3D12Device* device)
{
    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, _width, _height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
    CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_CLEAR_VALUE clear = {};
    clear.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clear.Color[3] = 0.0f;

    for (int i = 0; i < 2; i++)
    {
        ThrowIfFailed(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(_colorBuffer[i].ReleaseAndGetAddressOf())));
    }
}

void TAA::CreateSRVAndRTV(ID3D12Device* device)
{
    const D3D12_RESOURCE_DESC colorDesc = _colorBuffer[0].Get()->GetDesc();

    {
        for (int i = 0; i < 2; i++)
        {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Format = colorDesc.Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = colorDesc.MipLevels;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(_colorBuffer[i].Get(), &srvDesc, _srvRange.Cpu(i));
        }
    }

    {
        D3D12_RENDER_TARGET_VIEW_DESC colorRTVDesc = {};
        colorRTVDesc.Format = colorDesc.Format;
        colorRTVDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

        for (int i = 0; i < 2; i++)
        {
            device->CreateRenderTargetView(_colorBuffer[i].Get(), &colorRTVDesc, _rtvRange.Cpu(i));
        }
    }
}
