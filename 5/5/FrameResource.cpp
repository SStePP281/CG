#include "FrameResource.h"
#include "ThrowIfFaild.h"

FrameResource::FrameResource(ID3D12Device* device, UINT passCount, UINT materialCount, UINT lightCount, UINT shadowCascadeCount)
{
	ThrowIfFailed(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(CmdListAlloc.GetAddressOf())));

	PassCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);
	MaterialCB = std::make_unique<UploadBuffer<MaterialConstants>>(device, materialCount, true);

	LightSB = std::make_unique<UploadBuffer<LightConstants>>(device, lightCount, false);
	LightInfoCB = std::make_unique<UploadBuffer<LightInfoConstants>>(device, 1, true);

	TessellationCB = std::make_unique<UploadBuffer<TessellationConstant>>(device, 1, true);
	DisplacementCB = std::make_unique<UploadBuffer<DisplacementConstant>>(device, 1, true);

	InstanceDataSB = std::make_unique<UploadBuffer<InstanceData>>(device, 10000, false);

	ShadowCB = std::make_unique<UploadBuffer<ShadowConstant>>(device, shadowCascadeCount, true);
}
