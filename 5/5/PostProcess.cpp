#include "PostProcess.h"
#include "GBuffer.h"

// PASS

PostProcessPass::PostProcessPass(ID3D12Device* device, const PassDesc& desc, DescriptorAllocator* srvAlloc, DescriptorAllocator* rtvAlloc)
{
	Desc = desc;

	OutputSRV = srvAlloc->Allocate(1);
	OutputRTV = rtvAlloc->Allocate(1);

	CreateResources(device, srvAlloc, rtvAlloc);
}

void PostProcessPass::Build(ID3D12Device* device)
{
	BuildRootSignature(device);
	BuildPSO(device);
}

void PostProcessPass::Execute(ID3D12GraphicsCommandList* cmdList, DescriptorRange input, D3D12_CPU_DESCRIPTOR_HANDLE rtv)
{
	assert(input.Valid());

	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
	cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
	cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	cmdList->SetPipelineState(PSO.Get());
	cmdList->SetGraphicsRootSignature(RootSignature.Get());

	cmdList->SetGraphicsRootDescriptorTable(0, input.Gpu());

	for (int j = 0; j < (int)Desc.ExtraInputSrv.size(); j++)
	{
		cmdList->SetGraphicsRootDescriptorTable(1 + j, Desc.ExtraInputSrv[j].Gpu());
	}

	if (Desc.HasConstantBuffer && _constantBuffer)
	{
		int cbSlot = 1 + (int)Desc.ExtraInputSrv.size();
		cmdList->SetGraphicsRootConstantBufferView(cbSlot, _constantBuffer->GetGPUVirtualAddress());
	}

	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(3, 1, 0, 0);
}

void PostProcessPass::OnResize(ID3D12Device* device, Resolution res)
{
	Desc.Res = res;

	OutputBuffer.Reset();
	CreateOutputBuffer(device);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = Desc.OutputFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(OutputBuffer.Get(), &rtvDesc, OutputRTV.Cpu());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = Desc.OutputFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(OutputBuffer.Get(), &srvDesc, OutputSRV.Cpu());
}

void PostProcessPass::CreateResources(ID3D12Device* device, DescriptorAllocator* srvAlloc, DescriptorAllocator* rtvAlloc)
{
	CreateOutputBuffer(device);

	// RTV
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = Desc.OutputFormat;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	device->CreateRenderTargetView(OutputBuffer.Get(), &rtvDesc, OutputRTV.Cpu());

	// SRV
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = Desc.OutputFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(OutputBuffer.Get(), &srvDesc, OutputSRV.Cpu());

	if (Desc.HasConstantBuffer && Desc.ConstantBufferSize > 0)
	{
		UINT cbSize = (Desc.ConstantBufferSize + 255) & ~255;

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
		CD3DX12_RESOURCE_DESC bufDesc = CD3DX12_RESOURCE_DESC::Buffer(cbSize);

		ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_constantBuffer)));

		void* mapped = nullptr;
		_constantBuffer->Map(0, nullptr, &mapped);

		if (Desc.ConstantBufferData && mapped)
		{
			memcpy(mapped, Desc.ConstantBufferData, Desc.ConstantBufferSize);
		}

		_constantBuffer->Unmap(0, nullptr);
	}
}

void PostProcessPass::CreateOutputBuffer(ID3D12Device* device)
{
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = Desc.Res.Width;
	desc.Height = Desc.Res.Height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = Desc.OutputFormat;
	desc.SampleDesc = { 1, 0 };
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clear = {};
	clear.Format = Desc.OutputFormat;
	clear.Color[0] = clear.Color[1] = clear.Color[2] = 0.0f;
	clear.Color[3] = 1.0f;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&OutputBuffer)));
}

void PostProcessPass::BuildRootSignature(ID3D12Device* device)
{
	int totalTextures = 1 + (int)Desc.ExtraInputSrv.size();

	std::vector<CD3DX12_DESCRIPTOR_RANGE> ranges(totalTextures);
	std::vector<CD3DX12_ROOT_PARAMETER> params;

	for (int i = 0; i < totalTextures; i++)
	{
		ranges[i].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, i);
		CD3DX12_ROOT_PARAMETER p;
		p.InitAsDescriptorTable(1, &ranges[i], D3D12_SHADER_VISIBILITY_PIXEL);
		params.push_back(p);
	}

	if (Desc.HasConstantBuffer)
	{
		CD3DX12_ROOT_PARAMETER cbParam;
		cbParam.InitAsConstantBufferView(0);
		params.push_back(cbParam);
	}

	CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc((UINT)params.size(), params.data(), 1, &pointClamp, D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> serialized, error;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serialized.GetAddressOf(), error.GetAddressOf());

	if (error) OutputDebugStringA((char*)error->GetBufferPointer());
	ThrowIfFailed(hr);

	ThrowIfFailed(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&RootSignature)));
}

void PostProcessPass::BuildPSO(ID3D12Device* device)
{
	std::wstring wpath(Desc.ShaderPath.begin(), Desc.ShaderPath.end());
	std::wstring wvs(Desc.VS.begin(), Desc.VS.end());
	std::wstring wps(Desc.PS.begin(), Desc.PS.end());

	ComPtr<ID3DBlob> vsBlob = D3DUtil::CompileShader(wpath, nullptr, Desc.VS, "vs_5_1");
	ComPtr<ID3DBlob> psBlob = D3DUtil::CompileShader(wpath, nullptr, Desc.PS, "ps_5_1");

	D3D12_DEPTH_STENCIL_DESC dsDesc = {};
	dsDesc.DepthEnable = Desc.UseDepth ? TRUE : FALSE;
	dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
	dsDesc.StencilEnable = FALSE;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.pRootSignature = RootSignature.Get();

	psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
	psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = dsDesc;
	psoDesc.DSVFormat = Desc.UseDepth ? GBuffer::DEPTH_DSV_FORMAT : DXGI_FORMAT_UNKNOWN;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	psoDesc.NumRenderTargets = 1;
	psoDesc.RTVFormats[0] = Desc.OutputFormat;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	HRESULT hrPso = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&PSO));
	if (FAILED(hrPso))
	{
		char buf[256];
		sprintf_s(buf, "CreateGraphicsPipelineState failed: 0x%08X\n", hrPso);
		OutputDebugStringA(buf);
		ThrowIfFailed(hrPso);
	}
}

// CHAIN

void PostProcessChain::InitHDRBuffer(ID3D12Device* device, DescriptorAllocator* srvAlloc, DescriptorAllocator* rtvAlloc, Resolution res)
{
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(HDR_FORMAT, res.Width, res.Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

	D3D12_CLEAR_VALUE clear = {};
	clear.Format = HDR_FORMAT;
	clear.Color[3] = 1.0f;

	CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&_hdrBuffer)));

	_hdrSrv = srvAlloc->Allocate(1);
	_hdrRtv = rtvAlloc->Allocate(1);

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.Format = HDR_FORMAT;
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

	device->CreateRenderTargetView(_hdrBuffer.Get(), &rtvDesc, _hdrRtv.Cpu());

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = HDR_FORMAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;

	device->CreateShaderResourceView(_hdrBuffer.Get(), &srvDesc, _hdrSrv.Cpu());
}

DescriptorRange PostProcessChain::ReservePass(ID3D12Device* device, const PassDesc& desc, DescriptorAllocator* srvAlloc, DescriptorAllocator* rtvAlloc)
{
	if (desc.Stage == PostProcessStage::Tonemapping)
	{
		for (int i = 0; i < (int)_passes.size(); i++)
		{
			if (_passes[i]->Desc.Stage == PostProcessStage::Tonemapping)
			{
				throw std::runtime_error("PostProcessChain: Tonemapping pass already exists");
			}
		}
	}

	_passes.push_back(std::make_unique<PostProcessPass>(device, desc, srvAlloc, rtvAlloc));

	DescriptorRange out = _passes.back()->OutputSRV;
	Sort();

	return out;
}

void PostProcessChain::CommitAll(ID3D12Device* device)
{
	for (auto& pass : _passes)
	{
		pass->Build(device);
	}
}

PostProcessPass* PostProcessChain::GetPass(const std::string& name)
{
	for (int i = 0; i < _passes.size(); i++)
	{
		if (_passes[i]->Desc.Name == name) { return _passes[i].get(); }
	}

	return nullptr;
}

void PostProcessChain::ExecuteAll(ID3D12GraphicsCommandList* cmdList, DescriptorRange firstInput, DescriptorRange backBufferRTV)
{
	if (_passes.empty()) { return; }

	for (int i = 0; i < (int)_passes.size(); i++)
	{
		DescriptorRange input = (i == 0) ? firstInput : _passes[i - 1]->OutputSRV;

		bool isLast = (i == (int)_passes.size() - 1);
		DescriptorRange rtv = isLast ? backBufferRTV : _passes[i]->OutputRTV;

		if (!isLast)
		{
			auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(_passes[i]->OutputBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
			cmdList->ResourceBarrier(1, &toRT);

			_passes[i]->Execute(cmdList, input, rtv.Cpu());

			auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(_passes[i]->OutputBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
			cmdList->ResourceBarrier(1, &toSRV);
		}
		else
		{
			_passes[i]->Execute(cmdList, input, rtv.Cpu());
		}
	}
}

void PostProcessChain::OnResize(ID3D12Device* device, Resolution render, Resolution display)
{
	if (_hdrBuffer)
	{
		_hdrBuffer.Reset();

		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(HDR_FORMAT, render.Width, render.Height, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);

		D3D12_CLEAR_VALUE clear = {};
		clear.Format = HDR_FORMAT;
		clear.Color[3] = 1.0f;

		CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
		ThrowIfFailed(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, &clear, IID_PPV_ARGS(&_hdrBuffer)));

		D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
		rtvDesc.Format = HDR_FORMAT;
		rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		device->CreateRenderTargetView(_hdrBuffer.Get(), &rtvDesc, _hdrRtv.Cpu());

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = HDR_FORMAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = 1;
		device->CreateShaderResourceView(_hdrBuffer.Get(), &srvDesc, _hdrSrv.Cpu());
	}

	for (auto& pass : _passes)
	{
		pass->OnResize(device, display);
	}
}

void PostProcessChain::Sort()
{
	std::sort(_passes.begin(), _passes.end(),
		[](const std::unique_ptr<PostProcessPass>& a, const std::unique_ptr<PostProcessPass>& b)
		{
			if (a->Desc.Stage != b->Desc.Stage) { return (int)a->Desc.Stage < (int)b->Desc.Stage; }
			return a->Desc.Priority < b->Desc.Priority;
		});
}