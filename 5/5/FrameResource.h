#ifndef FRAME_RESOURCE_HPP
#define FRAME_RESOURCE_HPP

#include <memory>

#include <Windows.h>
#include <wrl/client.h>

#include <d3d12.h>

#include "UploadBuffer.h"
#include "MathHelper.h"
#include "D3DUtil.h"
#include "ModelStruct.h"

using namespace Microsoft::WRL;

struct PassConstants
{
    XMFLOAT4X4 View = MathHelper::Identity4x4();
    XMFLOAT4X4 InvView = MathHelper::Identity4x4();
    XMFLOAT4X4 Proj = MathHelper::Identity4x4();
    XMFLOAT4X4 InvProj = MathHelper::Identity4x4();
    XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    XMFLOAT4X4 InvViewProj = MathHelper::Identity4x4();
    XMFLOAT3 EyePosW = { 0.0f, 0.0f, 0.0f };
    float cbPerObjectPad1 = 0.0f;
    XMFLOAT2 RenderTargetSize = { 0.0f, 0.0f };
    XMFLOAT2 InvRenderTargetSize = { 0.0f, 0.0f };
    float NearZ = 0.0f;
    float FarZ = 0.0f;
    float TotalTime = 0.0f;
    float DeltaTime = 0.0f;

    XMFLOAT4 AmbientLight = { 0.0f, 0.0f, 0.0f, 1.0f };

    int DebugMode = 0;
    int DebugViewIndex = 0;
    int Pad[2] = { 0, 0 };
};

struct TessellationConstant
{
    TessellationConstant()
    {
        TessellationFactor = 8.0f;
        MaxTessellationDistance = 35.0f;
        Pad[0] = 0.0f; Pad[1] = 0.0f;
    }

    float TessellationFactor = 16.0f;
    float MaxTessellationDistance = 50.0f;
    float Pad[2] = { 0.0f, 0.0f };
};

struct DisplacementConstant
{
    DisplacementConstant()
    {
        DisplacementScale = 0.05f;
        DisplacementBias = -0.025f;
        Pad[0] = 0.0f; Pad[1] = 0.0f;
    }

    float DisplacementScale = 0.05f;
    float DisplacementBias = -0.02f;
    float Pad[2] = { 0.0f, 0.0f };
};

struct InstanceData
{
    XMFLOAT4X4 World = MathHelper::Identity4x4();
    XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();
};

struct ShadowConstant
{
    XMFLOAT4X4 ViewProj = MathHelper::Identity4x4();
    XMFLOAT4X4 ShadowTransform = MathHelper::Identity4x4();
    XMFLOAT4 Distances = { 50.0f, 200.0f, 1000.0f, 0.0f };
    XMFLOAT4 Padding[7];
};

struct FrameResource
{
public:

    FrameResource(ID3D12Device* device, UINT passCount, UINT materialCount, UINT lightCount, UINT shadowCascadeCount);
    FrameResource(const FrameResource& rhs) = delete;
    FrameResource& operator=(const FrameResource& rhs) = delete;
    ~FrameResource() {}

    ComPtr<ID3D12CommandAllocator> CmdListAlloc;

    std::unique_ptr<UploadBuffer<PassConstants>> PassCB = nullptr;
    std::unique_ptr<UploadBuffer<MaterialConstants>> MaterialCB = nullptr;
    std::unique_ptr<UploadBuffer<LightConstants>> LightSB = nullptr;
    std::unique_ptr<UploadBuffer<LightInfoConstants>> LightInfoCB = nullptr;

    std::unique_ptr<UploadBuffer<TessellationConstant>> TessellationCB = nullptr;
    std::unique_ptr<UploadBuffer<DisplacementConstant>> DisplacementCB = nullptr;

    std::unique_ptr<UploadBuffer<InstanceData>> InstanceDataSB = nullptr;

    std::unique_ptr<UploadBuffer<ShadowConstant>> ShadowCB = nullptr;

    UINT64 Fence = 0;
};

#endif // !FRAME_RESOURCE_HPP

