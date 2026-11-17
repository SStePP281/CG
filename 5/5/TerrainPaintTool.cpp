#include "TerrainPaintTool.h"
#include "ThrowIfFaild.h"
#include "D3DUtil.h"
#include "PathManager.h"

TerrainPaintTool::TerrainPaintTool(GraphicsDevice& device, Input& input, RenderGUI& gui) :
	_terrain(&terrain), _device(&device), _input(&input)
{
	_panel = &gui.AddPanel<TerrainPaintPanel>(_brush, _terrain->GetLayerCount());

	_descRange = device.Srv().Allocate(3);

	_brushUavSubRange = _descRange.Sub(0, 2);
	_hitSrvSubRange = _descRange.Sub(2);

	CreateHitBuffer();
	CreateSrvAndUAV();

	BuildPickPipeline();
	BuildRingPipeline();
	BuildBrushPipeline();
}

void TerrainPaintTool::Update(const GameTimer& gt)
{
	if (_input->MouseCaptured) { return; }

	bool ctrl = _input->IsDown(VK_CONTROL);
	bool shift = _input->IsDown(VK_SHIFT);

	if (ctrl || shift)
	{
		float notches = _input->ConsumeWheel();
		if (notches != 0.0f)
		{
			float k = powf(1.15f, notches);
			if (ctrl) { _brush.Radius = MathHelper::Clamp(_brush.Radius * k, BrushSettings::RADIUS_MIN, BrushSettings::RADIUS_MAX); }
			if (shift) { _brush.Strength = MathHelper::Clamp(_brush.Strength * k, BrushSettings::STRENGTH_MIN, BrushSettings::STRENGTH_MAX); }
		}
	}

	for (int vk = '1'; vk <= '9'; vk++)
	{
		UINT layer = vk - '1';
		if (_input->WasPressed(vk) && layer < _terrain->GetLayerCount()) { _brush.Layer = layer; }
	}
}

void TerrainPaintTool::PreDraw(const GameTimer& gt, const EditorFrameContext& ctx)
{
	TransitHitToWrite();

	Resolution display = ctx.window.Size();

	POINT m = _input->MousePos();
	float x = 2.0f * m.x / display.Width - 1.0f;
	float y = 1.0f - 2.0f * m.y / display.Height;

	DoPick(ctx.PassCB, x, y);

	if (!_input->MouseCaptured && _input->IsDown(VK_LBUTTON))
	{
		DoBrush();
	}

	TransitHitToRead();
}

void TerrainPaintTool::DrawOverlay(const EditorFrameContext& ctx)
{
	DoRing();
}

void TerrainPaintTool::DoPick(D3D12_GPU_VIRTUAL_ADDRESS passCB, float mouseNdcX, float mouseNdcY)
{
	ID3D12GraphicsCommandList* l = _device->CommandList();

	l->SetPipelineState(_terrainPickPSO.Get());
	l->SetComputeRootSignature(_rootSignatureTerrainPick.Get());

	l->SetComputeRootConstantBufferView(0, passCB);

	float c[8] = { mouseNdcX, mouseNdcY, BRUSH_MAX_DIST, TERRAIN_HEIGHT_SCALE, TERRAIN_HEIGHT_OFFSET, TERRAIN_WORLD_SIZE, TERRAIN_WORLD_OFFSET, TERRAIN_WORLD_OFFSET };
	l->SetComputeRoot32BitConstants(1, 8, c, 0);

	l->SetComputeRootDescriptorTable(2, _terrain->TexturesGpu().Gpu(0));
	l->SetComputeRootDescriptorTable(3, _brushUavSubRange.Gpu(1));

	l->Dispatch(1, 1, 1);

	auto uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(_hitBuffer.Get());
	l->ResourceBarrier(1, &uavBarrier);
}

void TerrainPaintTool::DoRing(D3D12_GPU_VIRTUAL_ADDRESS passCB)
{
	ID3D12GraphicsCommandList* l = _device->CommandList();

	l->SetPipelineState(_terrainRingPSO.Get());
	l->SetGraphicsRootSignature(_rootSignatureTerrainRing.Get());

	l->IASetVertexBuffers(0, 0, nullptr);
	l->IASetIndexBuffer(nullptr);
	l->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	l->SetGraphicsRootConstantBufferView(0, passCB);

	float strength01 = logf(_brushStrength / BRUSH_STRENGTH_MIN) / logf(BRUSH_STRENGTH_MAX / BRUSH_STRENGTH_MIN);
	float c[4] = { _brushRadius, _brushMode == BrushMode::Erase ? 1.0f : 0.0f, strength01, 0.0f };
	l->SetGraphicsRoot32BitConstants(1, 4, c, 0);

	l->SetGraphicsRootDescriptorTable(2, depthSrv.Gpu());
	l->SetGraphicsRootDescriptorTable(3, _hitSrvSubRange.Gpu());

	l->DrawInstanced(3, 1, 0, 0);
}

void TerrainPaintTool::DoBrush()
{
	ID3D12GraphicsCommandList* l = _device->CommandList();

	ID3D12Resource* maskArray = _terrain->GetMaskArray();
	int maskCount = _terrain->GetMaskCount();
	int layerCount = _terrain->GetLayerCount();

	auto toUav = CD3DX12_RESOURCE_BARRIER::Transition(maskArray, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	l->ResourceBarrier(1, &toUav);

	l->SetPipelineState(_terrainBrushPSO.Get());
	l->SetComputeRootSignature(_rootSignatureTerrainBrush.Get());

	struct { int layer; int mode; float radius; float strengthDt; int maskCount; int layerCount; float worldSize; int maskSize; } c =
	{ (int)layer, erase ? 1 : 0, radiusMeters, strengthDt, maskCount, layerCount, TERRAIN_WORLD_SIZE, (int)TERRAIN_MASK_SIZE };
	l->SetComputeRoot32BitConstants(0, 8, &c, 0);

	l->SetComputeRootDescriptorTable(1, _brushUavSubRange.Gpu());

	float texelMeters = TERRAIN_WORLD_SIZE / TERRAIN_MASK_SIZE;
	UINT side = 2 * (UINT)ceilf(radiusMeters / texelMeters) + 1;
	UINT groups = (side + 7) / 8;
	l->Dispatch(groups, groups, 1);

	auto toSrv = CD3DX12_RESOURCE_BARRIER::Transition(maskArray, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	l->ResourceBarrier(1, &toSrv);
}

void TerrainPaintTool::TransitHitToWrite()
{
	auto b = CD3DX12_RESOURCE_BARRIER::Transition(_hitBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	_device->CommandList()->ResourceBarrier(1, &b);
}

void TerrainPaintTool::TransitHitToRead()
{
	auto b = CD3DX12_RESOURCE_BARRIER::Transition(_hitBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	_device->CommandList()->ResourceBarrier(1, &b);
}

void TerrainPaintTool::CreateHitBuffer()
{
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(TerrainHit), D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	CD3DX12_HEAP_PROPERTIES heap(D3D12_HEAP_TYPE_DEFAULT);
	ThrowIfFailed(_device->Device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(_hitBuffer.GetAddressOf())));
}

void TerrainPaintTool::CreateSrvAndUAV()
{
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srv = {};
		srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srv.Format = DXGI_FORMAT_UNKNOWN;
		srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
		srv.Buffer.FirstElement = 0;
		srv.Buffer.NumElements = 1;
		srv.Buffer.StructureByteStride = sizeof(TerrainHit);
		srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
		_device->Device()->CreateShaderResourceView(_hitBuffer.Get(), &srv, _hitSrvSubRange.Cpu(0));
	}

	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
		uavDesc.Texture2DArray.MipSlice = 0;
		uavDesc.Texture2DArray.FirstArraySlice = 0;
		uavDesc.Texture2DArray.ArraySize = _terrain->GetMaskCount();

		_device->Device()->CreateUnorderedAccessView(_terrain->GetMaskArray(), nullptr, &uavDesc, _brushUavSubRange.Cpu(0));
	}

	{
		D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {};
		uav.Format = DXGI_FORMAT_UNKNOWN;
		uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
		uav.Buffer.FirstElement = 0;
		uav.Buffer.NumElements = 1;
		uav.Buffer.StructureByteStride = sizeof(TerrainHit);
		_device->Device()->CreateUnorderedAccessView(_hitBuffer.Get(), nullptr, &uav, _brushUavSubRange.Cpu(1));
	}
}

void TerrainPaintTool::BuildPickPipeline()
{
	{
		CD3DX12_DESCRIPTOR_RANGE heightRange;
		heightRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		CD3DX12_DESCRIPTOR_RANGE hitRange;
		hitRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[4];
		slotRootParameter[0].InitAsConstantBufferView(0);                                   // cbPass
		slotRootParameter[1].InitAsConstants(8, 1);                                         // cbPick (8 слов)
		slotRootParameter[2].InitAsDescriptorTable(1, &heightRange);                        // t0
		slotRootParameter[3].InitAsDescriptorTable(1, &hitRange);                           // u0

		CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}

		ThrowIfFailed(hr);

		ThrowIfFailed(_device->Device()->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&_rootSignatureTerrainPick)));
	}

	{
		ComPtr<ID3DBlob> blob = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/" + TERRAIN_FOLDER_W + L"TerrainPick.hlsl", nullptr, "CS", "cs_5_1");

		D3D12_COMPUTE_PIPELINE_STATE_DESC cpso = {};
		cpso.pRootSignature = _rootSignatureTerrainPick.Get();
		cpso.CS = { blob->GetBufferPointer(), blob->GetBufferSize() };
		ThrowIfFailed(_device->Device()->CreateComputePipelineState(&cpso, IID_PPV_ARGS(_terrainPickPSO.GetAddressOf())));
	}
}

void TerrainPaintTool::BuildRingPipeline()
{
	{
		CD3DX12_DESCRIPTOR_RANGE depthRange;
		depthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

		CD3DX12_DESCRIPTOR_RANGE hitRange;
		hitRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

		CD3DX12_ROOT_PARAMETER slotRootParameter[4];
		slotRootParameter[0].InitAsConstantBufferView(0);                                   // cbPass
		slotRootParameter[1].InitAsConstants(4, 1);
		slotRootParameter[2].InitAsDescriptorTable(1, &depthRange);                        // t0
		slotRootParameter[3].InitAsDescriptorTable(1, &hitRange);                          // t1

		CD3DX12_STATIC_SAMPLER_DESC sampler(1, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}

		ThrowIfFailed(hr);

		ThrowIfFailed(_device->Device()->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&_rootSignatureTerrainRing)));
	}

	{
		std::wstring path = LOCAL_PATH_W + L"Shaders/" + TERRAIN_FOLDER_W + L"TerrainRing.hlsl";
		ComPtr<ID3DBlob> blobVS = D3DUtil::CompileShader(path, nullptr, "VS", "vs_5_1");
		ComPtr<ID3DBlob> blobPS = D3DUtil::CompileShader(path, nullptr, "PS", "ps_5_1");

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};

		pso.pRootSignature = _rootSignatureTerrainRing.Get();
		pso.VS = { blobVS->GetBufferPointer(), blobVS->GetBufferSize() };
		pso.PS = { blobPS->GetBufferPointer(), blobPS->GetBufferSize() };

		pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);

		auto& rt = pso.BlendState.RenderTarget[0];
		rt.BlendEnable = TRUE;
		rt.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		rt.BlendOp = D3D12_BLEND_OP_ADD;
		rt.SrcBlendAlpha = D3D12_BLEND_ONE;
		rt.DestBlendAlpha = D3D12_BLEND_ZERO;
		rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;

		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthEnable = FALSE;

		pso.SampleMask = UINT_MAX;
		pso.SampleDesc.Count = 1;
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = _device->BackBufferFormat();
		pso.DSVFormat = DXGI_FORMAT_UNKNOWN;

		ThrowIfFailed(_device->Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(_terrainRingPSO.GetAddressOf())));
	}
}

void TerrainPaintTool::BuildBrushPipeline()
{
	{
		CD3DX12_DESCRIPTOR_RANGE uavRange;
		uavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0);    // u0 (маска), u1 (hit): слоты +3, +4

		CD3DX12_ROOT_PARAMETER slotRootParameter[2];
		slotRootParameter[0].InitAsConstants(8, 0);                              // cbBrush
		slotRootParameter[1].InitAsDescriptorTable(1, &uavRange);

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ComPtr<ID3DBlob> serializedRootSig = nullptr;
		ComPtr<ID3DBlob> errorBlob = nullptr;
		HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

		if (errorBlob != nullptr)
		{
			OutputDebugStringA((char*)errorBlob->GetBufferPointer());
		}

		ThrowIfFailed(hr);

		ThrowIfFailed(_device->Device()->CreateRootSignature(0, serializedRootSig->GetBufferPointer(), serializedRootSig->GetBufferSize(), IID_PPV_ARGS(&_rootSignatureTerrainBrush)));
	}

	{
		ComPtr<ID3DBlob> blob = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/" + TERRAIN_FOLDER_W + L"TerrainBrush.hlsl", nullptr, "CS", "cs_5_1");

		D3D12_COMPUTE_PIPELINE_STATE_DESC cpso = {};
		cpso.pRootSignature = _rootSignatureTerrainBrush.Get();
		cpso.CS = { blob->GetBufferPointer(), blob->GetBufferSize() };
		ThrowIfFailed(_device->Device()->CreateComputePipelineState(&cpso, IID_PPV_ARGS(_terrainBrushPSO.GetAddressOf())));
	}
}
