#include "AtmosphereData.h"
#include <d3dx12.h>
#include "ThrowIfFaild.h"
#include "LunaDDSTextureLoader.h"
#include "PathManager.h"

AtmosphereData::AtmosphereData(GraphicsDevice& device, const AtmosphereDesc& desc) : _desc(desc)
{
	_srv = device.Srv().Allocate(3);

	LoadTextures(device);
}

void AtmosphereData::LoadTextures(GraphicsDevice& device)
{
	std::vector<ComPtr<ID3D12Resource>> uploadHeaps;
	uploadHeaps.resize(3);

	auto LoadDDS = [&](const std::wstring& path, ComPtr<ID3D12Resource>& resource, bool isCubemap, int idx)
		{
			ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(device.Device(), device.CommandList(), path.c_str(), resource, uploadHeaps[idx]));

			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.Format = resource->GetDesc().Format;

			if (isCubemap)
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
				srvDesc.TextureCube.MostDetailedMip = 0;
				srvDesc.TextureCube.MipLevels = resource->GetDesc().MipLevels;
				srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
			}
			else
			{
				srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
				srvDesc.Texture2D.MostDetailedMip = 0;
				srvDesc.Texture2D.MipLevels = resource->GetDesc().MipLevels;
				srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
			}

			device.Device()->CreateShaderResourceView(resource.Get(), &srvDesc, _srv.Cpu(idx));
		};

	std::wstring hdriPath = LOCAL_PATH_W + L"HDRI/";

	LoadDDS(hdriPath + _desc.IrradianceFileName, _iblIrradiance, true, 0);
	LoadDDS(hdriPath + _desc.SpecularFileName, _iblSpecular, true, 1);
	LoadDDS(hdriPath + _desc.BrdfFileName, _iblBrdf, false, 2);

	for (auto& upload : uploadHeaps)
	{
		device.Release().Push(upload);
	}
}