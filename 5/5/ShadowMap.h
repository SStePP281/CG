#ifndef SHADOW_MAP_HPP
#define SHADOW_MAP_HPP

#include <Windows.h>
#include <wrl/client.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <dxgiformat.h>
#include <DirectXMath.h>
#include <functional>
#include "RenderSettings.h"
#include "GraphicsDevice.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;

class ShadowMap
{
public:
    static constexpr UINT CASCADE_COUNT = 3;

    static constexpr DXGI_FORMAT DEPTH_RESOURCE_FORMAT = DXGI_FORMAT_R24G8_TYPELESS;
    static constexpr DXGI_FORMAT DEPTH_DSV_FORMAT = DXGI_FORMAT_D24_UNORM_S8_UINT;
    static constexpr DXGI_FORMAT DEPTH_SRV_FORMAT = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;

    ShadowMap(GraphicsDevice& device, ShadowSettings& settings);

    void Render(ID3D12GraphicsCommandList* cmdList, const std::function<void(UINT cascade)>& drawCascade);

    ComPtr<ID3D12Resource> Resource;
    DescriptorRange SrvRange;
    DescriptorRange DsvRange;

private:
    void CreateTexture(ID3D12Device* device);
    void CreateSRVAndDSV(ID3D12Device* device);

    void TransitToOpaqueRenderingState(ID3D12GraphicsCommandList* cmdList);
    void TransitToLightRenderingState(ID3D12GraphicsCommandList* cmdList);

    void ClearView(ID3D12GraphicsCommandList* cmdList);

    ShadowSettings& _settings;
};

#endif // !SHADOW_MAP_HPP