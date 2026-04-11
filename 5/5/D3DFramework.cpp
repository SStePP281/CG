#define NOMINMAX

#include "D3DFramework.h"

#include <iostream>
#include <fstream> 
#include <d3dcompiler.h>
#include <array>
#include <algorithm>
#include <random>

#include "LunaDDSTextureLoader.h"
#include "D3DUtil.h"
#include "ThrowIfFaild.h"
#include "WICTextureLoader.h"
#include <ResourceUploadBatch.h>

using namespace DirectX;

D3DFramework::D3DFramework(HINSTANCE hInstance) : BaseD3DApp(hInstance) {}

D3DFramework::~D3DFramework() { if (_d3dDevice != nullptr) { FlushCommandQueue(); } }

bool D3DFramework::Initialize()
{
	if (!BaseD3DApp::Initialize()) { return false; }
	ThrowIfFailed(_cmdList->Reset(_directCmdListAlloc.Get(), nullptr));
	_cbvSrvDescriptorSize = _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	LoadModel("C:/Users/Stepan/Desktop/CG/5/Models/Model.obj");
	LoadModel("C:/Users/Stepan/Desktop/CG/5/Models/A_LOT_OF_POLYGONS.obj");
	//LoadModel("C:/Users/HUAWEI/Desktop/CG/5/Models/DispTest.obj");

	CreateLight();

	_gBuffer = std::make_unique<GBuffer>(_d3dDevice.Get(), CLIENT_WIDTH, CLIENT_HEIGHT);

	CreateSceneObjects();
	BuildRenderItems();

	BuildFrameResources();
	BuildLightSRV();

	BuildRootSignatureGBuffer();
	BuildRootSignatureLightPass();
	BuildDescriptorHeaps();
	BuildShadersAndInputLayout();

	BuildGBufferPSO();
	BuildLightPassPSO();

	ThrowIfFailed(_cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();

	BoundingBox sceneBounds({ 0, 0, 0 }, { 500, 500, 500 });
	_octreeRoot = std::make_unique<OctreeNode>(sceneBounds);

	for (auto& ri : _allRitems) {
		_octreeRoot->Insert(ri.get());
	}

	return true;
}

void D3DFramework::OnResize()
{
	BaseD3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&_proj, P);

	if (_gBuffer.get() != nullptr) { _gBuffer->OnResize(_d3dDevice.Get(), CLIENT_WIDTH, CLIENT_HEIGHT); }
}

void D3DFramework::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	_currFrameResourceIndex = (_currFrameResourceIndex + 1) % NUM_FRAME_RECOURCES;
	_currFrameResource = _frameResources[_currFrameResourceIndex].get();

	if (_currFrameResource->Fence != 0 && _fence->GetCompletedValue() < _currFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		ThrowIfFailed(_fence->SetEventOnCompletion(_currFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	AnimateMaterials(gt);
	AnimateLight(gt);

	UpdateObjectCBs(gt);
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateLightSB(gt);
}

void D3DFramework::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = _currFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(_cmdList->Reset(cmdListAlloc.Get(), nullptr));

	_cmdList->RSSetViewports(1, &_screenViewport);
	_cmdList->RSSetScissorRects(1, &_scissorRect);

	//GEOMETRY PASS (GBuffer)

	_gBuffer->TransitToOpaqueRenderingState(_cmdList.Get());
	_gBuffer->ClearView(_cmdList.Get());

	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 2> rtvs =
	{
		_gBuffer->Textures[(UINT)GBufferIndex::Albedo].RTV,
		_gBuffer->Textures[(UINT)GBufferIndex::Normal].RTV
	};

	auto dsv = _gBuffer->Textures[(UINT)GBufferIndex::Depth].DSV;
	_cmdList->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), TRUE, &dsv);

	_cmdList->SetPipelineState(_psos["gbuffer"].Get());
	_cmdList->SetGraphicsRootSignature(_rootSignatureGBuffer.Get());

	ID3D12DescriptorHeap* srv_heaps[] = { _srvDescriptorHeap.Get() };
	_cmdList->SetDescriptorHeaps(1, srv_heaps);

	auto passCB = _currFrameResource->PassCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(4, passCB->GetGPUVirtualAddress());

	auto tessCB = _currFrameResource->TessellationCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(6, tessCB->GetGPUVirtualAddress());

	auto dispCB = _currFrameResource->DisplacementCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(7, dispCB->GetGPUVirtualAddress());

	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
	
	_opaqueRitems.clear();
	if (_octreeRoot) {
		_octreeRoot->Query(_camFrustum, _opaqueRitems);
	}
	DrawRenderItems(_cmdList.Get(), _opaqueRitems);

	_gBuffer->TransitToLightRenderingState(_cmdList.Get());

	//LIGHTING PASS

	auto barrier1 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	_cmdList->ResourceBarrier(1, &barrier1);

	auto backBuffer = CurrentBackBufferView();
	_cmdList->OMSetRenderTargets(1, &backBuffer, FALSE, nullptr);
	_cmdList->ClearRenderTargetView(backBuffer, Colors::Black, 0, nullptr);

	_cmdList->SetPipelineState(_psos["lightPass"].Get());
	_cmdList->SetGraphicsRootSignature(_rootSignatureLightPass.Get());

	ID3D12DescriptorHeap* heaps[] = {
		_gBuffer->SRVHeap.Get(), // t0-t3
	};
	_cmdList->SetDescriptorHeaps(_countof(heaps), heaps);

	_cmdList->SetGraphicsRootDescriptorTable(0, _gBuffer->SRVHeap->GetGPUDescriptorHandleForHeapStart());

	passCB = _currFrameResource->PassCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	auto lightInfoCB = _currFrameResource->LightInfoCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(2, lightInfoCB->GetGPUVirtualAddress());

	// Fullscreen quad
	_cmdList->IASetVertexBuffers(0, 0, nullptr);
	_cmdList->IASetIndexBuffer(nullptr);
	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	if (_isDebugMode)
	{
		D3D12_RECT scissorRects[4] =
		{
			{ 0, 0, CLIENT_WIDTH / 2, CLIENT_HEIGHT / 2 },
			{ CLIENT_WIDTH / 2, 0, CLIENT_WIDTH, CLIENT_HEIGHT / 2 },
			{ 0, CLIENT_HEIGHT / 2, CLIENT_WIDTH / 2, CLIENT_HEIGHT },
			{ CLIENT_WIDTH / 2, CLIENT_HEIGHT / 2, CLIENT_WIDTH, CLIENT_HEIGHT }
		};

		UINT passElementSize = D3DUtil::CalcConstantBufferSize(sizeof(PassConstants));

		for (int i = 0; i < 4; i++)
		{
			PassConstants debugCB = _mainPassCB;
			debugCB.DebugMode = 1;
			debugCB.DebugViewIndex = i;

			_currFrameResource->PassCB->CopyData(i, debugCB);

			D3D12_GPU_VIRTUAL_ADDRESS passAddr = _currFrameResource->PassCB->Resource()->GetGPUVirtualAddress() + i * passElementSize;

			_cmdList->SetGraphicsRootConstantBufferView(1, passAddr);
			_cmdList->RSSetScissorRects(1, &scissorRects[i]);
			_cmdList->DrawInstanced(3, 1, 0, 0);
		}
	}
	else
	{
		D3D12_RECT fullRect = { 0, 0, CLIENT_WIDTH, CLIENT_HEIGHT };
		_cmdList->RSSetScissorRects(1, &fullRect);

		_mainPassCB.DebugMode = 0;
		_currFrameResource->PassCB->CopyData(0, _mainPassCB);

		_cmdList->SetGraphicsRootConstantBufferView(1, _currFrameResource->PassCB->Resource()->GetGPUVirtualAddress());

		_cmdList->DrawInstanced(3, 1, 0, 0);
	}

	auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	_cmdList->ResourceBarrier(1, &barrier2);

	//PRESENT

	ThrowIfFailed(_cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	ThrowIfFailed(_swapChain->Present(0, 0));
	_currBackBuffer = (_currBackBuffer + 1) % SWAP_CHAIN_BUFFER_COUNT;

	_currFrameResource->Fence = ++_currFence;
	_cmdQueue->Signal(_fence.Get(), _currFence);
}

void D3DFramework::OnMouseDown(WPARAM btnState, int x, int y)
{
	_lastMousePos.x = x;
	_lastMousePos.y = y;

	SetCapture(_hMainWnd);
}

void D3DFramework::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void D3DFramework::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(_rotateSpeed * (x - _lastMousePos.x));
		float dy = XMConvertToRadians(_rotateSpeed * (y - _lastMousePos.y));

		_yaw += dx;
		_pitch -= dy;

		_pitch = MathHelper::Clamp(_pitch, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);
	}

	_lastMousePos.x = x;
	_lastMousePos.y = y;
}

void D3DFramework::OnKeyboardInput(const GameTimer& gt)
{
	float dt = gt.DeltaTime();
	float speed = _moveSpeed * dt;

	XMVECTOR pos = DirectX::XMLoadFloat3(&_eyePos);
	XMVECTOR forward = XMLoadFloat3(&_forward);
	XMVECTOR right = XMLoadFloat3(&_right);

	if (GetAsyncKeyState('W') & 0x8000)
		pos += speed * forward;

	if (GetAsyncKeyState('S') & 0x8000)
		pos -= speed * forward;

	if (GetAsyncKeyState('A') & 0x8000)
		pos -= speed * right;

	if (GetAsyncKeyState('D') & 0x8000)
		pos += speed * right;

	if (GetAsyncKeyState(VK_F1) & 0x8000)
	{
		_isDebugMode = !_isDebugMode;

		Sleep(200);
	}

	XMStoreFloat3(&_eyePos, pos);
}

void D3DFramework::UpdateCamera(const GameTimer& gt)
{
	XMVECTOR forward = XMVectorSet(
		cosf(_pitch) * sinf(_yaw),
		sinf(_pitch),
		cosf(_pitch) * cosf(_yaw),
		0.0f);

	forward = XMVector3Normalize(forward);

	XMVECTOR right = XMVector3Normalize(XMVector3Cross(XMVectorSet(0, 1, 0, 0), forward));
	XMVECTOR up = XMVector3Cross(forward, right);

	XMStoreFloat3(&_forward, forward);
	XMStoreFloat3(&_right, right);
	XMStoreFloat3(&_up, up);

	XMVECTOR pos = XMLoadFloat3(&_eyePos);
	XMMATRIX view = XMMatrixLookToLH(pos, forward, up);

	XMStoreFloat4x4(&_view, view);
}

void D3DFramework::AnimateMaterials(const GameTimer& gt)
{
	//float t = gt.TotalTime();

	//for (auto& kv : _materials)
	//{
	//	Material* mat = kv.second.get();

	//	float pulse = 0.5f * sinf(2.0f * t) + 0.5f;
	//	mat->Data.MatTransform._11 = 0.8f + 0.2f * pulse;
	//	mat->Data.MatTransform._22 = mat->Data.MatTransform._11;

	//	mat->NumFramesDirty = NUM_FRAME_RECOURCES;
	//}
}

void D3DFramework::AnimateLight(const GameTimer& gt)
{
	/*float t = gt.TotalTime();

	for (auto& l : _lights)
	{
		l.Data.Position = { sin(t) * 1.0f, 2.0f, cos(t) * 1.0f};
		l.Data.Direction = { sin(t) * 1.0f, 1.0f, cos(t) * 1.0f };

		l.NumFramesDirty = NUM_FRAME_RECOURCES;
	}*/
}

void D3DFramework::UpdateObjectCBs(const GameTimer& gt)
{
	UploadBuffer<ObjectConstants>* currObjectCB = _currFrameResource->ObjectCB.get();

	for (auto& e : _allRitems)
	{
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			e->NumFramesDirty--;
		}
	}
}

void D3DFramework::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = _currFrameResource->MaterialCB.get();
	for (auto& e : _materials)
	{
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0)
		{
			currMaterialCB->CopyData(mat->MatCBIndex, mat->Data);

			mat->NumFramesDirty--;
		}
	}
}

void D3DFramework::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = XMLoadFloat4x4(&_view);
	XMMATRIX proj = XMLoadFloat4x4(&_proj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);
	XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

	XMStoreFloat4x4(&_mainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&_mainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&_mainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&_mainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&_mainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&_mainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

	_mainPassCB.EyePosW = _eyePos;
	_mainPassCB.RenderTargetSize = XMFLOAT2(CLIENT_WIDTH, CLIENT_HEIGHT);
	_mainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / CLIENT_WIDTH, 1.0f / CLIENT_HEIGHT);
	_mainPassCB.NearZ = 1.0f;
	_mainPassCB.FarZ = 1000.0f;
	_mainPassCB.TotalTime = gt.TotalTime();
	_mainPassCB.DeltaTime = gt.DeltaTime();
	_mainPassCB.DebugMode = _isDebugMode ? 1 : 0;

	auto currPassCB = _currFrameResource->PassCB.get();
	currPassCB->CopyData(0, _mainPassCB);

	TessellationConstant tess = TessellationConstant();
	auto tessCB = _currFrameResource->TessellationCB.get();
	tessCB->CopyData(0, tess);

	DisplacementConstant disp = DisplacementConstant();
	auto dispCB = _currFrameResource->DisplacementCB.get();
	dispCB->CopyData(0, disp);

	if (!_mainPassCB.DebugMode)
	{
		BoundingFrustum::CreateFromMatrix(_camFrustum, proj);
		_camFrustum.Transform(_camFrustum, invView);
	}
}

void D3DFramework::UpdateLightSB(const GameTimer& gt)
{
	auto currLightSB = _currFrameResource->LightSB.get();
	auto currLightInfo = _currFrameResource->LightInfoCB.get();

	int activeLight = 0;
	for (size_t i = 0; i < _lights.size(); ++i)
	{
		if (_lights[i].IsActive)
		{
			activeLight++;

			if (_lights[i].NumFramesDirty > 0)
			{
				currLightSB->CopyData((UINT)i, _lights[i].Data);
				_lights[i].NumFramesDirty--;
			}
		}
	}

	LightInfoConstants lightInfo = {};
	lightInfo.LightCount = activeLight;
	currLightInfo->CopyData(0, lightInfo);
}

void D3DFramework::BuildRootSignatureGBuffer()
{
	CD3DX12_DESCRIPTOR_RANGE defTable;
	defTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); //t0

	CD3DX12_DESCRIPTOR_RANGE normTable;
	normTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); //t1

	CD3DX12_DESCRIPTOR_RANGE dispTable;
	dispTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2); //t2

	CD3DX12_ROOT_PARAMETER slotRootParameter[8];
	slotRootParameter[0].InitAsDescriptorTable(1, &defTable, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[1].InitAsDescriptorTable(1, &normTable, D3D12_SHADER_VISIBILITY_ALL);
	slotRootParameter[2].InitAsDescriptorTable(1, &dispTable, D3D12_SHADER_VISIBILITY_ALL);

	slotRootParameter[3].InitAsConstantBufferView(0); //b0
	slotRootParameter[4].InitAsConstantBufferView(1); //b1
	slotRootParameter[5].InitAsConstantBufferView(2); //b2
	slotRootParameter[6].InitAsConstantBufferView(3); //b3
	slotRootParameter[7].InitAsConstantBufferView(4); //b4

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(8, slotRootParameter, (UINT)staticSamplers.size(), staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(_d3dDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignatureGBuffer)
	));
}

void D3DFramework::BuildRootSignatureLightPass()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[3];

	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

	slotRootParameter[1].InitAsConstantBufferView(1, 0, D3D12_SHADER_VISIBILITY_ALL);

	slotRootParameter[2].InitAsConstantBufferView(3, 0, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		3, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_NONE
	);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());

	ThrowIfFailed(hr);
	ThrowIfFailed(_d3dDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignatureLightPass)));
}

void D3DFramework::BuildDescriptorHeaps()
{
	UINT numTextures = (UINT)_textures.size();
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = numTextures > 0 ? numTextures : 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(_d3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&_srvDescriptorHeap)));

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(_srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	std::vector<Texture*> orderList;
	for (auto& kv : _textures) { orderList.push_back(kv.second.get()); }

	std::sort(orderList.begin(), orderList.end(), [](Texture* a, Texture* b) { return a->SrvHeapIndex < b->SrvHeapIndex; });

	for (auto& tex : orderList)
	{
		ComPtr<ID3D12Resource> res = tex->Resource;

		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = (res != nullptr) ? (UINT)res->GetDesc().MipLevels : 1;
		srvDesc.Format = (res != nullptr) ? res->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		_d3dDevice->CreateShaderResourceView(res.Get(), &srvDesc, hDescriptor);

		hDescriptor.Offset(1, _cbvSrvDescriptorSize);
	}
}

void D3DFramework::BuildLightSRV()
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = (UINT)_lights.size();
	srvDesc.Buffer.StructureByteStride = sizeof(LightConstants);
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;

	D3D12_CPU_DESCRIPTOR_HANDLE handle = _gBuffer->SRVHeap->GetCPUDescriptorHandleForHeapStart();
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(handle, 3, _d3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV));

	_d3dDevice->CreateShaderResourceView(_frameResources[0]->LightSB->Resource(), &srvDesc, cpuHandle);
}

void D3DFramework::BuildGBufferPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
	psoDesc.pRootSignature = _rootSignatureGBuffer.Get();

	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(_shaders["gbufferVS"]->GetBufferPointer()),
		_shaders["gbufferVS"]->GetBufferSize()
	};
	psoDesc.HS =
	{
		reinterpret_cast<BYTE*>(_shaders["gbufferHS"]->GetBufferPointer()),
		_shaders["gbufferHS"]->GetBufferSize()
	};
	psoDesc.DS =
	{
		reinterpret_cast<BYTE*>(_shaders["gbufferDS"]->GetBufferPointer()),
		_shaders["gbufferDS"]->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(_shaders["gbufferPS"]->GetBufferPointer()),
		_shaders["gbufferPS"]->GetBufferSize()
	};

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;

	psoDesc.NumRenderTargets = 2;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;

	psoDesc.DSVFormat = _depthStencilFormat;

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["gbuffer"])));
}

void D3DFramework::BuildLightPassPSO()
{
	CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

	CD3DX12_DEPTH_STENCIL_DESC dsDesc(D3D12_DEFAULT);
	dsDesc.DepthEnable = TRUE;
	dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { nullptr, 0 };
	psoDesc.pRootSignature = _rootSignatureLightPass.Get();
	psoDesc.BlendState = blendDesc;
	psoDesc.DepthStencilState = dsDesc;
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(_shaders["lightVS"]->GetBufferPointer()),
		_shaders["lightVS"]->GetBufferSize()
	};

	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(_shaders["lightPS"]->GetBufferPointer()),
		_shaders["lightPS"]->GetBufferSize()
	};

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	psoDesc.NumRenderTargets = 1;
	psoDesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;

	psoDesc.RTVFormats[0] = _backBufferFormat;
	psoDesc.DSVFormat = _depthStencilFormat;

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["lightPass"])));
}

void D3DFramework::BuildShadersAndInputLayout()
{
	_shaders["gbufferVS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/GBuffer.hlsl", nullptr, "VS", "vs_5_1");
	_shaders["gbufferPS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/GBuffer.hlsl", nullptr, "PS", "ps_5_1");

	_shaders["gbufferHS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/GBufferHS.hlsl", nullptr, "HS", "hs_5_1");
	_shaders["gbufferDS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/GBufferDS.hlsl", nullptr, "DS", "ds_5_1");

	_shaders["lightVS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/LightPass.hlsl", nullptr, "VS", "vs_5_1");
	_shaders["lightPS"] = D3DUtil::CompileShader(L"C:/Users/Stepan/Desktop/CG/5/Shaders/LightPass.hlsl", nullptr, "PS", "ps_5_1");

	_inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void D3DFramework::CreateLight()
{
	constexpr int LIGHT_COUNT = 10;

	auto RandomFloat = [&](float min, float max) -> float
		{
			static std::mt19937 gen(std::random_device{}());
			std::uniform_real_distribution<float> dis(min, max);
			return dis(gen);
		};

	{
		Light dirLight = {};

		dirLight.Data.Position = { 0.f, 0.f, 0.f };

		XMFLOAT3 dir = { -0.5f, -1.f, 0.3f };
		XMStoreFloat3(&dir, XMVector3Normalize(XMLoadFloat3(&dir)));
		dirLight.Data.Direction = dir;

		dirLight.Data.Strength = { 1.0f, 1.0f, 0.9f };

		dirLight.Data.FalloffStart = 0.f;
		dirLight.Data.FalloffEnd = 0.f;
		dirLight.Data.SpotPower = 0.f;

		dirLight.Data.LightType = static_cast<int>(LightType::Directional);
		dirLight.Data.Pad[0] = 0;
		dirLight.Data.Pad[1] = 0;
		dirLight.Data.Pad[2] = 0;

		dirLight.IsActive = true;
		dirLight.NumFramesDirty = NUM_FRAME_RECOURCES;
		dirLight.LightIndex = (int)_lights.size();

		_lights.push_back(dirLight);
	}

	for (int i = 0; i < LIGHT_COUNT; ++i)
	{
		float r = RandomFloat(0, 1);
		LightType type = LightType::Spot;

		XMFLOAT3 pos = {
			RandomFloat(-10.f, 10.f),
			RandomFloat(5.f, 15.f),
			RandomFloat(-10.f, 10.f)
		};

		XMFLOAT3 color = {
			RandomFloat(0.5f, 1.0f),
			RandomFloat(0.5f, 1.0f),
			RandomFloat(0.5f, 1.0f)
		};

		XMFLOAT3 dir = {
			RandomFloat(-1.f, 1.f),
			RandomFloat(-1.f, 0.f),
			RandomFloat(-1.f, 1.f)
		};
		XMStoreFloat3(&dir, XMVector3Normalize(XMLoadFloat3(&dir)));

		float falloffStart = RandomFloat(2.f, 5.f);
		float falloffEnd = RandomFloat(15.f, 30.f);
		float spotPower = RandomFloat(1.f, 1.f);

		Light light = {};
		light.Data.Strength = color;
		light.Data.FalloffStart = falloffStart;
		light.Data.Direction = dir;
		light.Data.FalloffEnd = falloffEnd;
		light.Data.Position = pos;
		light.Data.SpotPower = spotPower;
		light.Data.LightType = static_cast<int>(type);
		light.Data.Pad[0] = 0;
		light.Data.Pad[1] = 0;
		light.Data.Pad[2] = 0;
		light.IsActive = true;
		light.NumFramesDirty = NUM_FRAME_RECOURCES;
		light.LightIndex = (int)_lights.size();

		_lights.push_back(light);
	}
}

void D3DFramework::LoadModel(std::string path)
{
	ModelParse geoGen;
	ModelParse::MeshInfo meshData = geoGen.LoadOBJ(path);

	ParseMesh(meshData);
	LoadTextures(meshData);
	ParseMaterials(meshData);
}

void D3DFramework::CreateSceneObjects()
{
	if (_models.empty()) { return; }

	for (auto& model : _models)
	{
		if (model.second->Mesh->Name == "C:/Users/Stepan/Desktop/CG/5/Models/A_LOT_OF_POLYGONS.obj")
		{
			XMINT3 gridInstanceCount = XMINT3(10, 10, 10);
			float spacing = 1.0f;

			float offsetX = (gridInstanceCount.x * spacing) / 2.0f;
			float offsetY = (gridInstanceCount.y * spacing) / 2.0f;
			float offsetZ = (gridInstanceCount.z * spacing) / 2.0f;

			for (int x = 0; x < gridInstanceCount.x; x++)
			{
				for (int y = 0; y < gridInstanceCount.y; y++)
				{
					for (int z = 0; z < gridInstanceCount.z; z++)
					{
						auto sceneObj = std::make_unique<SceneObject>();
						sceneObj->ModelData = model.second.get();

						float px = x * spacing - offsetX;
						float py = y * spacing - offsetY;
						float pz = z * spacing - offsetZ;

						XMMATRIX world = XMMatrixTranslation(px, py, pz);
						XMStoreFloat4x4(&sceneObj->World, world);

						_sceneObjects.push_back(std::move(sceneObj));
					}
				}
			}
		}
		else
		{
			auto sceneObj = std::make_unique<SceneObject>();
			sceneObj->ModelData = model.second.get();
			XMStoreFloat4x4(&sceneObj->World, XMMatrixIdentity());

			_sceneObjects.push_back(std::move(sceneObj));
		}
	}
}

void D3DFramework::BuildFrameResources()
{
	for (int i = 0; i < NUM_FRAME_RECOURCES; ++i)
	{
		_frameResources.push_back(std::make_unique<FrameResource>(_d3dDevice.Get(),
			(UINT)4,
			(UINT)_allRitems.size(),
			(UINT)_materials.size(),
			(UINT)_lights.size()));
	}
}

void D3DFramework::BuildRenderItems()
{
	_allRitems.clear();
	_opaqueRitems.clear();

	UINT objIndex = 0;
	for (auto& objPtr : _sceneObjects)
	{
		SceneObject* obj = objPtr.get();
		Model* model = obj->ModelData;

		if (!model || !model->Mesh) { continue; }

		for (const Model::Part& part : model->Parts)
		{
			auto ri = std::make_unique<RenderItem>();

			auto& sub = model->Mesh->DrawArgs[part.SubmeshName];

			ri->Geo = model->Mesh;
			ri->Bounds = BoundingBox(sub.Bounds);
			ri->UsedPso = part.SubmeshName == "opaque";

			if (!part.MaterialName.empty() && _materials.count(part.MaterialName)) { ri->Mat = _materials[part.MaterialName].get(); }
			else if (!_materials.empty()) { ri->Mat = _materials.begin()->second.get(); }
			else { ri->Mat = nullptr; }

			ri->IndexCount = sub.IndexCount;
			ri->StartIndexLocation = sub.StartIndexLocation;
			ri->BaseVertexLocation = sub.BaseVertexLocation;

			ri->World = obj->World;
			ri->ObjCBIndex = objIndex++;

			_allRitems.push_back(std::move(ri));

			RenderItem* rawPtr = _allRitems.back().get();
			_opaqueRitems.push_back(rawPtr);
		}
	}
}

void D3DFramework::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = D3DUtil::CalcConstantBufferSize(sizeof(ObjectConstants));
	UINT matCBByteSize = D3DUtil::CalcConstantBufferSize(sizeof(MaterialConstants));

	auto objectCB = _currFrameResource->ObjectCB->Resource();
	auto matCB = _currFrameResource->MaterialCB->Resource();

	for (auto ri : ritems)
	{
		if (!ri || !ri->Mat) { continue; }

		auto vertexBuffer = ri->Geo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);

		auto indexBuffer = ri->Geo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexBuffer);

		int diffuseIndex = ri->Mat->DiffuseSrvHeapIndex;
		int normalIndex = ri->Mat->NormalSrvHeapIndex;
		int dispIndex = ri->Mat->DisplacementSrvHeapIndex;

		if (diffuseIndex < 0) { diffuseIndex = 0; }
		if (normalIndex < 0) { normalIndex = 0; }
		if (dispIndex < 0) { dispIndex = 0; }

		CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseHandle(_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		diffuseHandle.Offset(diffuseIndex, _cbvSrvDescriptorSize);

		CD3DX12_GPU_DESCRIPTOR_HANDLE normalHandle(_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		normalHandle.Offset(normalIndex, _cbvSrvDescriptorSize);

		CD3DX12_GPU_DESCRIPTOR_HANDLE dispHandle(_srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		dispHandle.Offset(dispIndex, _cbvSrvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, diffuseHandle);
		cmdList->SetGraphicsRootDescriptorTable(1, normalHandle);
		cmdList->SetGraphicsRootDescriptorTable(2, dispHandle);

		cmdList->SetGraphicsRootConstantBufferView(3, objCBAddress);
		cmdList->SetGraphicsRootConstantBufferView(5, matCBAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> D3DFramework::GetStaticSamplers()
{
	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

void D3DFramework::ParseMesh(const ModelParse::MeshInfo& meshData)
{
	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = meshData.MeshName;

	UINT vbByteSize = (UINT)meshData.Vertices.size() * sizeof(Vertex);
	UINT ibByteSize = (UINT)meshData.Indices32.size() * sizeof(uint32_t);

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), meshData.Vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), meshData.Indices32.data(), ibByteSize);

	geo->VertexBufferGPU = D3DUtil::CreateDefaultBuffer
	(
		_d3dDevice.Get(),
		_cmdList.Get(),
		meshData.Vertices.data(),
		vbByteSize,
		geo->VertexBufferUploader
	);

	geo->IndexBufferGPU = D3DUtil::CreateDefaultBuffer
	(
		_d3dDevice.Get(),
		_cmdList.Get(),
		meshData.Indices32.data(),
		ibByteSize,
		geo->IndexBufferUploader
	);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexBufferByteSize = ibByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;

	for (const auto& sub : meshData.Submeshes)
	{
		SubmeshGeometry subGeo;
		subGeo.IndexCount = sub.IndexCount;
		subGeo.StartIndexLocation = sub.IndexOffset;
		subGeo.BaseVertexLocation = sub.VertexOffset;
		subGeo.Bounds = sub.Bounds;

		geo->DrawArgs[sub.Name] = subGeo;
	}

	_geometries[geo->Name] = std::move(geo);

	auto model = std::make_unique<Model>();
	model->Mesh = _geometries[meshData.MeshName].get();

	for (const auto& sub : meshData.Submeshes)
	{
		Model::Part p;
		p.SubmeshName = sub.Name;
		p.MaterialName = sub.MaterialName;
		model->Parts.push_back(std::move(p));
	}

	_models[meshData.MeshName] = std::move(model);
}

void D3DFramework::LoadTextures(const ModelParse::MeshInfo& meshData)
{
	const std::string DEFAULT_TEXTURE[3] =
	{
		"T_DEFAULT_COLOR.png",
		"T_DEFAULT_NORMAL.png",
		"T_DEFAULT_DISPLACEMENT.png"
	};

	if (_textures.size() == 0)
	{
		for (auto& texName : DEFAULT_TEXTURE)
		{
			auto tex = std::make_unique<Texture>();
			tex->Name = texName;
			tex->Filename = L"C:/Users/Stepan/Desktop/CG/5/DefaultTextures/" + std::wstring(texName.begin(), texName.end());

			ResourceUploadBatch resourceUpload(_d3dDevice.Get());
			resourceUpload.Begin();

			ThrowIfFailed(DirectX::CreateWICTextureFromFile(_d3dDevice.Get(), resourceUpload, tex->Filename.c_str(), tex->Resource.ReleaseAndGetAddressOf(), true));

			auto uploadResourcesFinished = resourceUpload.End(_cmdQueue.Get());
			uploadResourcesFinished.wait();

			tex->SrvHeapIndex = (int)_textures.size();

			_textures[texName] = std::move(tex);
		}
	}

	for (auto& kv : meshData.Materials)
	{
		auto& mi = kv.second;

		std::vector<std::string> texturesToLoad;

		if (!mi.DiffuseTextureName.empty())
			texturesToLoad.push_back(mi.DiffuseTextureName);

		if (!mi.NormalTextureName.empty())
			texturesToLoad.push_back(mi.NormalTextureName);

		if (!mi.DisplacementTextureName.empty())
			texturesToLoad.push_back(mi.DisplacementTextureName);

		for (auto& texName : texturesToLoad)
		{
			if (_textures.count(texName) != 0) { continue; }

			auto tex = std::make_unique<Texture>();
			tex->Name = texName;
			tex->Filename = L"C:/Users/Stepan/Desktop/CG/5/Textures/" + std::wstring(texName.begin(), texName.end());

			ResourceUploadBatch resourceUpload(_d3dDevice.Get());
			resourceUpload.Begin();

			ThrowIfFailed(DirectX::CreateWICTextureFromFile(_d3dDevice.Get(), resourceUpload, tex->Filename.c_str(), tex->Resource.ReleaseAndGetAddressOf(), true));

			auto uploadResourcesFinished = resourceUpload.End(_cmdQueue.Get());
			uploadResourcesFinished.wait();

			tex->SrvHeapIndex = (int)_textures.size();

			_textures[texName] = std::move(tex);
		}
	}
}

void D3DFramework::ParseMaterials(const ModelParse::MeshInfo& meshData)
{
	for (auto& kv : meshData.Materials)
	{
		std::string matName = kv.first;
		auto& mi = kv.second;

		if (_materials.count(matName) != 0) { continue; }

		auto mat = std::make_unique<Material>();
		mat->Name = matName;
		mat->MatCBIndex = (int)_materials.size();

		MaterialConstants data;

		data.DiffuseAlbedo = XMFLOAT4(mi.DiffuseColor.x, mi.DiffuseColor.y, mi.DiffuseColor.z, mi.DiffuseColor.w);

		if (_textures.count(mi.DiffuseTextureName))
		{
			mat->DiffuseSrvHeapIndex = _textures[mi.DiffuseTextureName]->SrvHeapIndex;
		}
		else
		{
			mat->DiffuseSrvHeapIndex = 0;
		}

		if (_textures.count(mi.NormalTextureName))
		{
			mat->NormalSrvHeapIndex = _textures[mi.NormalTextureName]->SrvHeapIndex;
		}
		else
		{
			mat->NormalSrvHeapIndex = 1;
		}

		if (_textures.count(mi.DisplacementTextureName))
		{
			mat->DisplacementSrvHeapIndex = _textures[mi.DisplacementTextureName]->SrvHeapIndex;
		}
		else
		{
			mat->DisplacementSrvHeapIndex = 2;
		}

		data.FresnelR0 = { 0.01f, 0.01f, 0.01f };
		data.Roughness = 0.3f;

		if (mat->Name == "Mat")
		{
			data.MaxTessellationFactor = 4.0f;
			data.MaxTessellationDistance = 15.0f;
		}
		else if (mat->Name == "Mat.1")
		{
			data.MaxTessellationFactor = 1.0f;
			data.MaxTessellationDistance = 150.0f;
		}

		mat->Data = std::move(data);
		_materials[matName] = std::move(mat);
	}
}
