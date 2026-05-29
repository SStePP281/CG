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

#define HOME 0

#if HOME == 1

const std::string LOCAL_PATH = "C:/Users/Stepan/Desktop/CG/5/";
const std::wstring LOCAL_PATH_W = L"C:/Users/Stepan/Desktop/CG/5/";

#else

const std::string LOCAL_PATH = "C:/Users/HUAWEI/Desktop/CG/5/";
const std::wstring LOCAL_PATH_W = L"C:/Users/HUAWEI/Desktop/CG/5/";

#endif // HOME

using namespace DirectX;

D3DFramework::D3DFramework(HINSTANCE hInstance) : BaseD3DApp(hInstance) {}

D3DFramework::~D3DFramework() { if (_d3dDevice != nullptr) { FlushCommandQueue(); } }

bool D3DFramework::Initialize()
{
	if (!BaseD3DApp::Initialize()) { return false; }
	ThrowIfFailed(_cmdList->Reset(_directCmdListAlloc.Get(), nullptr));

	LoadModel(LOCAL_PATH + "Models/Model.obj");
	//LoadModel("C:/Users/Stepan/Desktop/CG/5/Models/A_LOT_OF_POLYGONS.obj");
	//LoadModel("C:/Users/HUAWEI/Desktop/CG/5/Models/DispTest.obj");

	CreateLight();

	BuildDescriptorHeaps();

	_gBufferSrvStart = _lastSlot;
	CD3DX12_CPU_DESCRIPTOR_HANDLE gBufHandle(_srvHeap->GetCPUDescriptorHandleForHeapStart(), _gBufferSrvStart, _srvDescriptorSize);
	_gBuffer = std::make_unique<GBuffer>(_d3dDevice.Get(), CLIENT_WIDTH, CLIENT_HEIGHT, gBufHandle, _srvDescriptorSize);
	_lastSlot += (UINT)GBufferIndex::Count;

	_shadowSrvStart = _lastSlot++;
	CD3DX12_CPU_DESCRIPTOR_HANDLE shadowHandle(_srvHeap->GetCPUDescriptorHandleForHeapStart(), _shadowSrvStart, _srvDescriptorSize);
	_shadowMap = std::make_unique<ShadowMap>(_d3dDevice.Get(), shadowHandle, _srvDescriptorSize);

	_ppChain = std::make_unique<PostProcessChain>();
	CreatePPS();

	CreateSceneObjects();
	BuildRenderItems();
	ComputeSceneBounds();

	BuildFrameResources();
	BuildLightSRV();

	BuildRootSignatureGBuffer();
	BuildRootSignatureLightPass();
	BuildRootSignatureShadowPass();
	
	BuildShadersAndInputLayout();

	BuildGBufferPSO();
	BuildLightPassPSO();
	BuildShadowPassPSO();

	ThrowIfFailed(_cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { _cmdList.Get() };
	_cmdQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	FlushCommandQueue();

	_octreeRoot = std::make_unique<OctreeNode>(_sceneBounds);

	for (auto& ri : _allRitems)
	{
		for (uint32_t i = 0; i < (uint32_t)ri->Instances.size(); i++)
		{
			_octreeRoot->Insert(ri.get(), i);
		}
	}

	return true;
}

void D3DFramework::OnResize()
{
	BaseD3DApp::OnResize();

	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&_proj, P);

	if (_gBuffer.get() != nullptr) { _gBuffer->OnResize(_d3dDevice.Get(), CLIENT_WIDTH, CLIENT_HEIGHT); }
	if (_ppChain.get() != nullptr) { _ppChain->OnResize(_d3dDevice.Get(), _srvHeap.Get(), _srvDescriptorSize, CLIENT_WIDTH, CLIENT_HEIGHT); }
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

	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateLightSB(gt);
	UpdateShadowCB(gt);

	UpdateInstanceData(gt);
}

void D3DFramework::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = _currFrameResource->CmdListAlloc;
	ThrowIfFailed(cmdListAlloc->Reset());
	ThrowIfFailed(_cmdList->Reset(cmdListAlloc.Get(), nullptr));

	//SHADOW PASS

	D3D12_VIEWPORT shadowViewport = { 0, 0, SHADOW_MAP_TEXTURE_DEFAULT_SIZE, SHADOW_MAP_TEXTURE_DEFAULT_SIZE, 0.0f, 1.0f };
	D3D12_RECT shadowScissor = { 0, 0, SHADOW_MAP_TEXTURE_DEFAULT_SIZE, SHADOW_MAP_TEXTURE_DEFAULT_SIZE };
	
	_cmdList->RSSetViewports(1, &shadowViewport);
	_cmdList->RSSetScissorRects(1, &shadowScissor);

	_shadowMap->TransitToOpaqueRenderingState(_cmdList.Get());
	_shadowMap->ClearView(_cmdList.Get());

	_cmdList->SetPipelineState(_psos["shadowPass"].Get());
	_cmdList->SetGraphicsRootSignature(_rootSignatureShadowPass.Get());

	ID3D12DescriptorHeap* heaps[] = { _srvHeap.Get() };
	_cmdList->SetDescriptorHeaps(1, heaps);

	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	auto shadowCB = _currFrameResource->ShadowCB->Resource();
	UINT shadowElementSize = D3DUtil::CalcConstantBufferSize(sizeof(ShadowConstant));
	for (int k = 0; k < CASCADES_COUNT; k++)
	{
		D3D12_GPU_VIRTUAL_ADDRESS shadowAddr = shadowCB->GetGPUVirtualAddress() + k * shadowElementSize;
		_cmdList->SetGraphicsRootConstantBufferView(1, shadowAddr);

		auto dsv = _shadowMap->DSVs[k];
		_cmdList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

		DrawRenderItemsShadow(_cmdList.Get(), _shadowRitems);
	}

	_shadowMap->TransitToLightRenderingState(_cmdList.Get());

	//GEOMETRY PASS (GBuffer)

	_cmdList->RSSetViewports(1, &_screenViewport);
	_cmdList->RSSetScissorRects(1, &_scissorRect);

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

	auto passCB = _currFrameResource->PassCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(3, passCB->GetGPUVirtualAddress());

	auto tessCB = _currFrameResource->TessellationCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(5, tessCB->GetGPUVirtualAddress());

	auto dispCB = _currFrameResource->DisplacementCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(6, dispCB->GetGPUVirtualAddress());

	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);
	
	DrawRenderItems(_cmdList.Get(), _opaqueRitems);

	_gBuffer->TransitToLightRenderingState(_cmdList.Get());

	//LIGHTING PASS

	auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(_ppChain->First()->OutputBuffer.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	_cmdList->ResourceBarrier(1, &toRT);

	float clearColor[4] = { 0, 0, 0, 1 };
	auto lightTarget = _ppChain->GetPass("ToneMapping");
	_cmdList->ClearRenderTargetView(lightTarget->OutputRTV, clearColor, 0, nullptr);

	D3D12_CPU_DESCRIPTOR_HANDLE depth = _gBuffer->Textures[(UINT)GBufferIndex::Depth].DSV;
	_cmdList->OMSetRenderTargets(1, &lightTarget->OutputRTV, FALSE, &depth);

	_cmdList->SetPipelineState(_psos["lightPass"].Get());
	_cmdList->SetGraphicsRootSignature(_rootSignatureLightPass.Get());

	CD3DX12_GPU_DESCRIPTOR_HANDLE albedoGpu(_srvHeap->GetGPUDescriptorHandleForHeapStart(), _gBufferSrvStart + 0, _srvDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE normalGpu(_srvHeap->GetGPUDescriptorHandleForHeapStart(), _gBufferSrvStart + 1, _srvDescriptorSize);
	CD3DX12_GPU_DESCRIPTOR_HANDLE depthGpu (_srvHeap->GetGPUDescriptorHandleForHeapStart(), _gBufferSrvStart + 2, _srvDescriptorSize);

	_cmdList->SetGraphicsRootDescriptorTable(0, albedoGpu);
	_cmdList->SetGraphicsRootDescriptorTable(1, normalGpu);
	_cmdList->SetGraphicsRootDescriptorTable(2, depthGpu);
	_cmdList->SetGraphicsRootShaderResourceView(3, _currFrameResource->LightSB->Resource()->GetGPUVirtualAddress());

	passCB = _currFrameResource->PassCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(4, passCB->GetGPUVirtualAddress());

	auto lightInfoCB = _currFrameResource->LightInfoCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(5, lightInfoCB->GetGPUVirtualAddress());

	auto shadowCBRes = _currFrameResource->ShadowCB->Resource();
	_cmdList->SetGraphicsRootConstantBufferView(6, shadowCBRes->GetGPUVirtualAddress());

	CD3DX12_GPU_DESCRIPTOR_HANDLE shadowGpu(_srvHeap->GetGPUDescriptorHandleForHeapStart(), _shadowSrvStart, _srvDescriptorSize);
	_cmdList->SetGraphicsRootDescriptorTable(7, shadowGpu);

	// Fullscreen quad
	_cmdList->IASetVertexBuffers(0, 0, nullptr);
	_cmdList->IASetIndexBuffer(nullptr);
	_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	if (_isDebugMode)
	{
		std::vector<D3D12_RECT> scissorRects = BuildScissorRects(MAX_DEBUG_LAYER_COUNT, CLIENT_WIDTH, CLIENT_HEIGHT);

		UINT passElementSize = D3DUtil::CalcConstantBufferSize(sizeof(PassConstants));

		for (int i = 0; i < MAX_DEBUG_LAYER_COUNT; i++)
		{
			PassConstants debugCB = _mainPassCB;
			debugCB.DebugMode = 1;
			debugCB.DebugViewIndex = i;

			_currFrameResource->PassCB->CopyData(i, debugCB);

			D3D12_GPU_VIRTUAL_ADDRESS passAddr = _currFrameResource->PassCB->Resource()->GetGPUVirtualAddress() + i * passElementSize;

			_cmdList->SetGraphicsRootConstantBufferView(4, passAddr);
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

		_cmdList->SetGraphicsRootConstantBufferView(4, _currFrameResource->PassCB->Resource()->GetGPUVirtualAddress());

		_cmdList->DrawInstanced(3, 1, 0, 0);
	}

	auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(_ppChain->First()->OutputBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	_cmdList->ResourceBarrier(1, &toSRV);

	//POST_PROCESS

	auto barrierToPP = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	_cmdList->ResourceBarrier(1, &barrierToPP);

	_ppChain->ExecuteAll(_cmdList.Get(), _srvHeap.Get(), _srvDescriptorSize, CurrentBackBufferView());

	auto barrierToPresent = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	_cmdList->ResourceBarrier(1, &barrierToPresent);

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
	//float angle = gt.TotalTime() * 0.314f + XM_PIDIV4;

	//for (int i = 0; i < (int)_lights.size(); i++)
	//{
	//	if (_lights[i].Data.LightType != (int)LightType::Directional) { continue; }

	//	XMVECTOR dir = XMVector3Normalize(XMVectorSet(cosf(angle), -sinf(angle), 0.0f, 0.0f));

	//	XMStoreFloat3(&_lights[i].Data.Direction, dir);
	//	_lights[i].NumFramesDirty = NUM_FRAME_RECOURCES;
	//}
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

void D3DFramework::UpdateShadowCB(const GameTimer& gt)
{
	const float cascadeNears[] = { 1.0f,  50.0f,  200.0f };
	const float cascadeFars[] = { 50.0f, 200.0f, 1000.0f };

	XMMATRIX view = XMLoadFloat4x4(&_view);

	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f
	);

	auto currShadowCB = _currFrameResource->ShadowCB.get();

	for (int lightIdx = 0; lightIdx < (int)_lights.size(); lightIdx++)
	{
		if (_lights[lightIdx].Data.LightType != (int)LightType::Directional) { continue; }

		XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&_lights[lightIdx].Data.Direction));

		XMVECTOR up = XMVectorSet(0, 1, 0, 0);
		if (fabsf(XMVectorGetY(lightDir)) > 0.99f) { up = XMVectorSet(1, 0, 0, 0); }

		for (int cascade = 0; cascade < CASCADES_COUNT; cascade++)
		{
			float nearZ = cascadeNears[cascade];
			float farZ = cascadeFars[cascade];

			XMMATRIX cascadeProj = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), nearZ, farZ);

			XMMATRIX invVP = XMMatrixInverse(nullptr, XMMatrixMultiply(view, cascadeProj));

			XMVECTOR corners[8];
			int idx = 0;
			for (int x = 0; x < 2; x++)
			{
				for (int y = 0; y < 2; y++)
				{
					for (int z = 0; z < 2; z++)
					{
						XMVECTOR pt = XMVectorSet(2.0f * x - 1.0f, 2.0f * y - 1.0f, (float)z, 1.0f);
						pt = XMVector4Transform(pt, invVP);
						corners[idx++] = pt / XMVectorGetW(pt);
					}
				}
			}

			XMVECTOR center = XMVectorZero();
			for (int i = 0; i < 8; i++) { center += corners[i]; }
			center /= 8.0f;

			XMMATRIX lightView = XMMatrixLookAtLH(center - lightDir * farZ, center, up);

			float minX = FLT_MAX, maxX = -FLT_MAX;
			float minY = FLT_MAX, maxY = -FLT_MAX;
			float minZ = FLT_MAX, maxZ = -FLT_MAX;

			for (int i = 0; i < 8; i++)
			{
				XMVECTOR v = XMVector3TransformCoord(corners[i], lightView);
				float vx = XMVectorGetX(v), vy = XMVectorGetY(v), vz = XMVectorGetZ(v);
				minX = std::min(minX, vx); maxX = std::max(maxX, vx);
				minY = std::min(minY, vy); maxY = std::max(maxY, vy);
				minZ = std::min(minZ, vz); maxZ = std::max(maxZ, vz);
			}

			float zMult = 10.0f;
			minZ = (minZ < 0) ? minZ * zMult : minZ / zMult;
			maxZ = (maxZ < 0) ? maxZ / zMult : maxZ * zMult;

			float worldTexelSizeX = (maxX - minX) / SHADOW_MAP_TEXTURE_DEFAULT_SIZE;
			float worldTexelSizeY = (maxY - minY) / SHADOW_MAP_TEXTURE_DEFAULT_SIZE;
			minX = floorf(minX / worldTexelSizeX) * worldTexelSizeX;
			maxX = floorf(maxX / worldTexelSizeX) * worldTexelSizeX;
			minY = floorf(minY / worldTexelSizeY) * worldTexelSizeY;
			maxY = floorf(maxY / worldTexelSizeY) * worldTexelSizeY;

			XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(minX, maxX, minY, maxY, minZ, maxZ);

			XMMATRIX lightViewProj = XMMatrixMultiply(lightView, lightProj);

			XMVECTOR originNDC = XMVector3TransformCoord(XMVectorZero(), lightViewProj);
			float texelSizeNDC = 2.0f / SHADOW_MAP_TEXTURE_DEFAULT_SIZE;
			float snapX = floorf(XMVectorGetX(originNDC) / texelSizeNDC) * texelSizeNDC;
			float snapY = floorf(XMVectorGetY(originNDC) / texelSizeNDC) * texelSizeNDC;
			float dx = XMVectorGetX(originNDC) - snapX;
			float dy = XMVectorGetY(originNDC) - snapY;
			lightViewProj = XMMatrixMultiply(lightViewProj, XMMatrixTranslation(-dx, -dy, 0.0f));

			ShadowConstant sc;
			XMStoreFloat4x4(&sc.ViewProj, XMMatrixTranspose(lightViewProj));
			XMStoreFloat4x4(&sc.ShadowTransform, XMMatrixTranspose(XMMatrixMultiply(lightViewProj, T)));
			sc.Distances = { cascadeFars[0], cascadeFars[1], cascadeFars[2], 0.0f };

			currShadowCB->CopyData(cascade, sc);
		}

		break;
	}
}

void D3DFramework::UpdateInstanceData(const GameTimer& gt)
{
	_opaqueRitems.clear();
	_shadowRitems.clear();

	auto currInstanceBuffer = _currFrameResource->InstanceDataSB.get();

	int shadowOffset = 0;

	for (auto& riPtr : _allRitems)
	{
		RenderItem* ri = riPtr.get();
		ri->ShadowInstanceOffset = (UINT)shadowOffset;
		ri->ShadowInstanceCount = (UINT)ri->Instances.size();

		for (const auto& inst : ri->Instances)
		{
			InstanceData data;
			XMStoreFloat4x4(&data.World, XMMatrixTranspose(XMLoadFloat4x4(&inst.World)));
			XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(XMLoadFloat4x4(&inst.TexTransform)));
			currInstanceBuffer->CopyData(shadowOffset++, data);
		}

		if (ri->ShadowInstanceCount > 0) { _shadowRitems.push_back(ri); }
	}

	std::vector<OctreeNode::InstanceRef> visibleInstances;
	if (_octreeRoot)
	{
		_octreeRoot->Query(_camFrustum, visibleInstances);
	}

	for (auto& riPtr : _allRitems)
	{
		riPtr->VisibleInstanceCount = 0;
		riPtr->InstanceOffset = (UINT)shadowOffset;
	}

	std::unordered_map<RenderItem*, std::vector<uint32_t>> grouped;
	for (const auto& ref : visibleInstances)
	{
		grouped[ref.Item].push_back(ref.Index);
	}

	int cameraOffset = shadowOffset;
	for (auto& pair : grouped)
	{
		RenderItem* ri = pair.first;
		ri->InstanceOffset = (UINT)cameraOffset;
		ri->VisibleInstanceCount = (UINT)pair.second.size();

		for (uint32_t idx : pair.second)
		{
			InstanceData data;
			XMStoreFloat4x4(&data.World, XMMatrixTranspose(XMLoadFloat4x4(&ri->Instances[idx].World)));
			XMStoreFloat4x4(&data.TexTransform, XMMatrixTranspose(XMLoadFloat4x4(&ri->Instances[idx].TexTransform)));
			currInstanceBuffer->CopyData(cameraOffset++, data);
		}

		_opaqueRitems.push_back(ri);
	}
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

	slotRootParameter[3].InitAsConstantBufferView(0); //b0 cbPass
	slotRootParameter[4].InitAsConstantBufferView(1); //b1 cbMat
	slotRootParameter[5].InitAsConstantBufferView(2); //b2 cbTess
	slotRootParameter[6].InitAsConstantBufferView(3); //b3 cbDisp

	slotRootParameter[7].InitAsShaderResourceView(3, 1, D3D12_SHADER_VISIBILITY_ALL); //t3

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
	CD3DX12_DESCRIPTOR_RANGE albedoRange; albedoRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0
	CD3DX12_DESCRIPTOR_RANGE normalRange; normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1); // t1
	CD3DX12_DESCRIPTOR_RANGE depthRange;  depthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);  // t2
	CD3DX12_DESCRIPTOR_RANGE shadowRange; shadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4); // t4

	CD3DX12_ROOT_PARAMETER slotRootParameter[8];
	slotRootParameter[0].InitAsDescriptorTable(1, &albedoRange); // Albedo    t0
	slotRootParameter[1].InitAsDescriptorTable(1, &normalRange); // Normal    t1
	slotRootParameter[2].InitAsDescriptorTable(1, &depthRange);  // Depth     t2
	slotRootParameter[3].InitAsShaderResourceView(3);            // LightSB   t3
	slotRootParameter[4].InitAsConstantBufferView(0);            // PassCB    b0
	slotRootParameter[5].InitAsConstantBufferView(1);            // LightInfo b1
	slotRootParameter[6].InitAsConstantBufferView(2);            // ShadowCB  b2
	slotRootParameter[7].InitAsDescriptorTable(1, &shadowRange); // ShadowMap t4

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(8, slotRootParameter, (UINT)staticSamplers.size(), staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());

	ThrowIfFailed(hr);
	ThrowIfFailed(_d3dDevice->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignatureLightPass)));
}

void D3DFramework::BuildRootSignatureShadowPass()
{
	CD3DX12_ROOT_PARAMETER slotRootParameter[2];
	slotRootParameter[0].InitAsShaderResourceView(0, 1);
	slotRootParameter[1].InitAsConstantBufferView(0);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, 0, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

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
		IID_PPV_ARGS(&_rootSignatureShadowPass)
	));
}

void D3DFramework::BuildDescriptorHeaps()
{
	std::vector<Texture*> orderList;
	for (auto& kv : _textures) { orderList.push_back(kv.second.get()); }
	std::sort(orderList.begin(), orderList.end(), [](Texture* a, Texture* b) { return a->SrvHeapIndex < b->SrvHeapIndex; });

	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(_srvHeap->GetCPUDescriptorHandleForHeapStart());

	for (auto& tex : orderList)
	{
		ComPtr<ID3D12Resource> res = tex->Resource;
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MostDetailedMip = 0;
		srvDesc.Texture2D.MipLevels = res ? (UINT)res->GetDesc().MipLevels : 1;
		srvDesc.Format = res ? res->GetDesc().Format : DXGI_FORMAT_R8G8B8A8_UNORM;
		srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

		_d3dDevice->CreateShaderResourceView(res.Get(), &srvDesc, hDescriptor);
		hDescriptor.Offset(1, _srvDescriptorSize);
	}

	_lastSlot = (UINT)orderList.size();
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

	_lightSbSrvSlot = _lastSlot;
	CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle(_srvHeap->GetCPUDescriptorHandleForHeapStart(), _lightSbSrvSlot, _srvDescriptorSize);
	_lastSlot += 1;

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

	psoDesc.RTVFormats[0] = HDR_FORMAT;
	psoDesc.DSVFormat = _depthStencilFormat;

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["lightPass"])));
}

void D3DFramework::BuildShadowPassPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
	psoDesc.pRootSignature = _rootSignatureShadowPass.Get();
	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);

	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(_shaders["shadowVS"]->GetBufferPointer()),
		_shaders["shadowVS"]->GetBufferSize()
	};

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	psoDesc.RasterizerState.DepthBias = 5000;
	psoDesc.RasterizerState.SlopeScaledDepthBias = 1.5f;
	psoDesc.RasterizerState.DepthBiasClamp = 0.0f;

	psoDesc.NumRenderTargets = 0;
	psoDesc.SampleDesc.Count = _4xMsaaState ? 4 : 1;
	psoDesc.SampleDesc.Quality = _4xMsaaState ? (_4xMsaaQuality - 1) : 0;

	psoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	psoDesc.DSVFormat = _depthStencilFormat;

	ThrowIfFailed(_d3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["shadowPass"])));
}

void D3DFramework::BuildShadersAndInputLayout()
{
	_shaders["gbufferVS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/GBuffer.hlsl", nullptr, "VS", "vs_5_1");
	_shaders["gbufferPS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/GBuffer.hlsl", nullptr, "PS", "ps_5_1");

	_shaders["gbufferHS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/GBufferHS.hlsl", nullptr, "HS", "hs_5_1");
	_shaders["gbufferDS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/GBufferDS.hlsl", nullptr, "DS", "ds_5_1");

	_shaders["lightVS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/LightPass.hlsl", nullptr, "VS", "vs_5_1");
	_shaders["lightPS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/LightPass.hlsl", nullptr, "PS", "ps_5_1");

	_shaders["shadowVS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/Shadow.hlsl", nullptr, "VS", "vs_5_1");

	_inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void D3DFramework::ComputeSceneBounds()
{
	XMFLOAT3 minPt = { FLT_MAX,  FLT_MAX,  FLT_MAX };
	XMFLOAT3 maxPt = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

	for (auto& ri : _allRitems)
	{
		for (auto& inst : ri->Instances)
		{
			BoundingBox worldBounds;
			XMMATRIX world = XMLoadFloat4x4(&inst.World);
			ri->Bounds.Transform(worldBounds, world);

			XMFLOAT3 bMin =
			{
				worldBounds.Center.x - worldBounds.Extents.x,
				worldBounds.Center.y - worldBounds.Extents.y,
				worldBounds.Center.z - worldBounds.Extents.z
			};
			XMFLOAT3 bMax =
			{
				worldBounds.Center.x + worldBounds.Extents.x,
				worldBounds.Center.y + worldBounds.Extents.y,
				worldBounds.Center.z + worldBounds.Extents.z
			};

			minPt.x = std::min(minPt.x, bMin.x);
			minPt.y = std::min(minPt.y, bMin.y);
			minPt.z = std::min(minPt.z, bMin.z);
			maxPt.x = std::max(maxPt.x, bMax.x);
			maxPt.y = std::max(maxPt.y, bMax.y);
			maxPt.z = std::max(maxPt.z, bMax.z);
		}
	}

	_sceneBounds.Center =
	{
		(minPt.x + maxPt.x) * 0.5f,
		(minPt.y + maxPt.y) * 0.5f,
		(minPt.z + maxPt.z) * 0.5f
	};

	_sceneBounds.Extents =
	{
		(maxPt.x - minPt.x) * 0.5f,
		(maxPt.y - minPt.y) * 0.5f,
		(maxPt.z - minPt.z) * 0.5f
	};
}

void D3DFramework::CreatePPS()
{
	// OUTLINE
	struct OutlineConstants
	{
		DirectX::XMFLOAT2 TexelSize;
		float KernelSize = 3.0f;
		float DepthThreshold = 0.0f;
		float NormalThreshold = 0.0f;
		float pad[3];
		DirectX::XMFLOAT3 OutlineColor = { 0.0f, 0.0f, 0.0f };
		float pad2;
	};

	OutlineConstants outlineCB = {};
	outlineCB.TexelSize = { 1.0f / CLIENT_WIDTH, 1.0f / CLIENT_HEIGHT };
	outlineCB.KernelSize = 3.0f;
	outlineCB.DepthThreshold = 0.0001f;
	outlineCB.NormalThreshold = 0.1f;
	outlineCB.OutlineColor = { 0.0f, 0.0f, 0.0f };

	PassDesc outlineDesc = {};
	outlineDesc.Name = "Outline";
	outlineDesc.ShaderPath = LOCAL_PATH + "Shaders/Outline.hlsl";
	outlineDesc.Stage = PostProcessStage::BeforeTonemapping;
	outlineDesc.Priority = 0;
	outlineDesc.OutputFormat = HDR_FORMAT;
	outlineDesc.Width = CLIENT_WIDTH;
	outlineDesc.Height = CLIENT_HEIGHT;
	outlineDesc.HasConstantBuffer = true;
	outlineDesc.ConstantBufferSize = sizeof(OutlineConstants);
	outlineDesc.ConstantBufferData = &outlineCB;

	// TONE_MAPPING
	PassDesc toneDesc = {};
	toneDesc.Name = "ToneMapping";
	toneDesc.ShaderPath = LOCAL_PATH + "Shaders/ToneMapping.hlsl";
	toneDesc.Stage = PostProcessStage::Tonemapping;
	toneDesc.OutputFormat = _backBufferFormat;
	toneDesc.Width = CLIENT_WIDTH;
	toneDesc.Height = CLIENT_HEIGHT;

	UINT toneSlot = _ppChain->ReservePass(_d3dDevice.Get(), toneDesc, _srvHeap.Get(), _lastSlot, _srvDescriptorSize);
	UINT outlineSlot = _ppChain->ReservePass(_d3dDevice.Get(), outlineDesc, _srvHeap.Get(), _lastSlot, _srvDescriptorSize);

	_ppChain->GetPass("Outline")->Desc.InputSrvSlots = { _gBufferSrvStart + 2, _gBufferSrvStart + 1, toneSlot };

	_ppChain->GetPass("ToneMapping")->Desc.InputSrvSlots = { outlineSlot };

	_ppChain->CommitAll(_d3dDevice.Get(), _srvHeap.Get(), _srvDescriptorSize);
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

		dirLight.Data.Position = { 0.0f, 5.0f, 0.0f };

		XMFLOAT3 dir = { 1.0f, -1.0f, 0.0f };
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
		if (model.second->Mesh->Name == LOCAL_PATH + "Models / A_LOT_OF_POLYGONS.obj")
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
	int dirLightCount = 0;
	for (int i = 0; i < _lights.size(); i++)
	{
		if (_lights[i].Data.LightType == (int)LightType::Directional)
		{
			dirLightCount++;
		}
	}

	for (int i = 0; i < NUM_FRAME_RECOURCES; ++i)
	{
		_frameResources.push_back(std::make_unique<FrameResource>(_d3dDevice.Get(), (UINT)MAX_DEBUG_LAYER_COUNT, (UINT)_materials.size(), (UINT)_lights.size(), dirLightCount * CASCADES_COUNT));
	}
}

void D3DFramework::BuildRenderItems()
{
	_allRitems.clear();
	_opaqueRitems.clear();

	std::unordered_map<std::string, std::unique_ptr<RenderItem>> ritemsMap;

	for (auto& objPtr : _sceneObjects)
	{
		SceneObject* obj = objPtr.get();
		Model* model = obj->ModelData;

		if (!model || !model->Mesh) { continue; }

		for (const Model::Part& part : model->Parts)
		{
			std::string matName = part.MaterialName.empty() ? "DefaultMat" : part.MaterialName;
			std::string key = model->Mesh->Name + "_" + part.SubmeshName + "_" + matName;

			if (ritemsMap.find(key) == ritemsMap.end())
			{
				auto ri = std::make_unique<RenderItem>();
				auto& sub = model->Mesh->DrawArgs[part.SubmeshName];

				ri->Geo = model->Mesh;
				ri->SubmeshName = part.SubmeshName;
				ri->Bounds = BoundingBox(sub.Bounds);

				if (_materials.count(matName))
				{
					ri->Mat = _materials[matName].get();
				}
				else if (!_materials.empty())
				{
					ri->Mat = _materials.begin()->second.get();
				}

				ri->IndexCount = sub.IndexCount;
				ri->StartIndexLocation = sub.StartIndexLocation;
				ri->BaseVertexLocation = sub.BaseVertexLocation;

				ri->UsedPso = (part.SubmeshName == "opaque") ? "opaque" : "opaque";

				ritemsMap[key] = std::move(ri);
			}

			InstanceData inst;
			inst.World = obj->World;
			XMStoreFloat4x4(&inst.TexTransform, XMMatrixIdentity());

			ritemsMap[key]->Instances.push_back(inst);
		}
	}

	for (auto& kv : ritemsMap)
	{
		_allRitems.push_back(std::move(kv.second));
		_opaqueRitems.push_back(_allRitems.back().get());
	}
}

void D3DFramework::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT matCBByteSize = D3DUtil::CalcConstantBufferSize(sizeof(MaterialConstants));

	auto matCB = _currFrameResource->MaterialCB->Resource();

	auto instanceBuffer = _currFrameResource->InstanceDataSB->Resource();
	UINT instanceByteSize = sizeof(InstanceData);

	for (RenderItem* ri : ritems)
	{
		if (!ri || !ri->Mat || ri->VisibleInstanceCount == 0) { continue; }

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

		CD3DX12_GPU_DESCRIPTOR_HANDLE diffuseHandle(_srvHeap->GetGPUDescriptorHandleForHeapStart(), diffuseIndex, _srvDescriptorSize);
		CD3DX12_GPU_DESCRIPTOR_HANDLE normalHandle (_srvHeap->GetGPUDescriptorHandleForHeapStart(), normalIndex,  _srvDescriptorSize);
		CD3DX12_GPU_DESCRIPTOR_HANDLE dispHandle (_srvHeap->GetGPUDescriptorHandleForHeapStart(), dispIndex,    _srvDescriptorSize);

		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS instAddress = instanceBuffer->GetGPUVirtualAddress() + (ri->InstanceOffset * instanceByteSize);

		cmdList->SetGraphicsRootDescriptorTable(0, diffuseHandle);
		cmdList->SetGraphicsRootDescriptorTable(1, normalHandle);
		cmdList->SetGraphicsRootDescriptorTable(2, dispHandle);

		cmdList->SetGraphicsRootConstantBufferView(4, matCBAddress);
		cmdList->SetGraphicsRootShaderResourceView(7, instAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, ri->VisibleInstanceCount, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

void D3DFramework::DrawRenderItemsShadow(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	auto instanceBuffer = _currFrameResource->InstanceDataSB->Resource();
	UINT instanceByteSize = sizeof(InstanceData);

	for (RenderItem* ri : ritems)
	{
		if (!ri || ri->ShadowInstanceCount == 0) { continue; }

		auto vertexBuffer = ri->Geo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);

		auto indexBuffer = ri->Geo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexBuffer);

		D3D12_GPU_VIRTUAL_ADDRESS instAddress = instanceBuffer->GetGPUVirtualAddress() + (ri->ShadowInstanceOffset * instanceByteSize);
		cmdList->SetGraphicsRootShaderResourceView(0, instAddress);

		cmdList->DrawIndexedInstanced(ri->IndexCount, ri->ShadowInstanceCount, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> D3DFramework::GetStaticSamplers()
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

	const CD3DX12_STATIC_SAMPLER_DESC shadow(
		6,
		D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		D3D12_TEXTURE_ADDRESS_MODE_BORDER,
		0.0f,
		16,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp, shadow };
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
			tex->Filename = LOCAL_PATH_W + L"DefaultTextures/" + std::wstring(texName.begin(), texName.end());

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
			tex->Filename = LOCAL_PATH_W + L"Textures/" + std::wstring(texName.begin(), texName.end());

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
