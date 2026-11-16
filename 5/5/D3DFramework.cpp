#define NOMINMAX

#include "D3DFramework.h"

#include <cmath>
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
#include "NoAA.h"
#include "TAA.h"
#include "DLAA.h"

#include "PathManager.h"
#include <map>

using namespace DirectX;

D3DFramework::D3DFramework(Window& window, Input& input, AssetManager& assets, World& world, GraphicsDevice& device) :
	_window(&window), _input(input), _assets(&assets), _world(&world), _device(&device)
{
	UpdateRenderResolution();
}

D3DFramework::~D3DFramework() { if (_device) { _device->Flush(); } }

bool D3DFramework::Initialize()
{
	OnDisplayResize(_window->Size());

	_device->BeginImmediate();

	_ngxManager = std::make_unique<NGXManager>(_device->Device());
	if (!_ngxManager->IsInitialized())
	{
		MessageBox(0, L"RegisterNGXManager Failed.", 0, 0);
	}

	{
		auto sampler = GetStaticSamplers();

		AAInitContext ctx = { *_device, sampler.data(), (UINT)sampler.size() };

		_noAA = std::make_unique<NoAA>();
		_TAA = std::make_unique<TAA>(ctx);
		_DLAA = std::make_unique<DLAA>(_ngxManager.get(), ctx);

		_currentAA = GetAAByMode(_renderSettings.Mode);

		UpdateRenderResolution();
		OnRenderResize();

		_noAA->OnResize(_device->Device(), _device->CommandList(), _renderSettings);
		_TAA->OnResize(_device->Device(), _device->CommandList(), _renderSettings);
		_DLAA->OnResize(_device->Device(), _device->CommandList(), _renderSettings);
	}

	_gBuffer = std::make_unique<GBuffer>(*_device, _renderSettings.Render);

	_shadowMap = std::make_unique<ShadowMap>(*_device, _shadowSettings);

	_terrain = std::make_unique<Terrain>(*_device);

	_velocityBuffer = std::make_unique<VelocityBuffers>(*_device);
	_velocityBuffer->OnResize(_device->Device(), _renderSettings.Render);

	_ppChain = std::make_unique<PostProcessChain>();
	CreatePPS();

	LoadIBLTextures();

	BuildRenderItems();
	BuildFrameResources();

	BuildRootSignatureGBuffer();
	BuildRootSignatureLightPass();
	BuildRootSignatureShadowPass();
	BuildRootSignatureTerrainPass();

	BuildShadersAndInputLayout();

	BuildGBufferPSO();
	BuildLightPassPSO();
	BuildShadowPassPSO();
	BuildTerrainPassPSO();

	_gui = std::make_unique<RenderGUI>(_window->Handle(), *_device, _renderSettings.SwapChainBufferCount);
	_window->MessageHook = [this](HWND h, UINT m, WPARAM w, LPARAM l)
		{
			return _gui && _gui->HandleMessage(h, m, w, l);
		};

	_gui->AddPanel<InfoPanel>(_frameStats, _frameHistory);

	_device->EndImmediate();

	return true;
}

void D3DFramework::UpdateRenderResolution()
{
	_renderSettings.Render = _currentAA ? _currentAA->ComputeRenderResolution(_renderSettings) : _renderSettings.Display;
}

void D3DFramework::OnDisplayResize(Resolution display)
{
	_renderSettings.Display = display;

	_screenViewport = { 0.0f, 0.0f, (float)display.Width, (float)display.Height, 0.0f, 1.0f };
	_scissorRect = { 0, 0, (LONG)display.Width, (LONG)display.Height };

	UpdateRenderResolution();
	OnRenderResize();
}

void D3DFramework::OnRenderResize()
{
	_renderViewport = _screenViewport;
	_renderViewport.Width = (float)_renderSettings.Render.Width;
	_renderViewport.Height = (float)_renderSettings.Render.Height;

	_renderScissor = { 0, 0, (LONG)_renderSettings.Render.Width, (LONG)_renderSettings.Render.Height };

	_device->Flush();

	ID3D12Device* d = _device->Device();
	ID3D12GraphicsCommandList* l = _device->CommandList();

	if (_gBuffer.get() != nullptr) { _gBuffer->OnResize(d, _renderSettings.Render); }
	if (_ppChain.get() != nullptr) { _ppChain->OnResize(d, _renderSettings.Render, _renderSettings.Display); }
	if (_velocityBuffer.get() != nullptr) { _velocityBuffer->OnResize(d, _renderSettings.Render); }

	if (_TAA != nullptr) { _TAA->OnResize(d, l, _renderSettings); }
	if (_DLAA != nullptr) { _DLAA->OnResize(d, l, _renderSettings); }
	if (_currentAA != nullptr) { _currentAA->ResetHistory(); }
}

void D3DFramework::Update(const GameTimer& gt)
{
	_device->BeginFrame();
	_currFrameResource = _frameResources[_device->FrameIndex()].get();

	_gui->NewFrame();

	_input.MouseCaptured = ImGui::GetIO().WantCaptureMouse;
	_input.KeyboardCaptured = ImGui::GetIO().WantCaptureKeyboard;

	_frameHistory.Push(gt.DeltaTime() * 1000.0f);

	_frameStats.ResetPerFrame();
	_frameStats.SrvUsed = _device->Srv().Used();
	_frameStats.SrvCapacity = _device->Srv().Capacity();
	_frameStats.RtvUsed = _device->Rtv().Used();
	_frameStats.RtvCapacity = _device->Rtv().Capacity();

	OnKeyboardInput(gt);
	UpdateCamera(gt);

	if (_renderSettingsDirty)
	{
		UpdateRenderResolution();
		OnRenderResize();
		_renderSettingsDirty = false;
	}

	AnimateMaterials(gt);
	AnimateLight(gt);

	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
	UpdateLightSB(gt);
	UpdateShadowCB(gt);
	UpdateTerrainCB(gt);

	UpdateInstanceData(gt);
}

void D3DFramework::Draw(const GameTimer& gt)
{
	ID3D12GraphicsCommandList* l = _device->CommandList();

	//TERRAIN DRAW MODE

	_terrain->TransitHitToWrite(l);
	if (_paintMode)
	{
		POINT m = _input.MousePos();
		float mouseX = 2.0f * m.x / _renderSettings.Display.Width - 1.0f;
		float mouseY = 1.0f - 2.0f * m.y / _renderSettings.Display.Height;

		_terrain->DoPick(l, _currFrameResource->PassCB->Resource()->GetGPUVirtualAddress(), mouseX, mouseY, 1000.0f);

		if (GetAsyncKeyState(VK_LBUTTON) & 0x8000)
		{
			_terrain->DoBrush(l, _brushLayer, _brushMode == BrushMode::Erase, _brushRadius, _brushStrength * gt.DeltaTime());
		}
	}
	_terrain->TransitHitToRead(l);

	//SHADOW PASS

	bool containsDirLight = false;
	for (const auto& light : _world->Lights())
	{
		if (light.Active && light.Type == LightType::Directional)
		{
			containsDirLight = true;
			break;
		}
	}

	if (containsDirLight)
	{
		l->SetPipelineState(_psos["shadowPass"].Get());
		l->SetGraphicsRootSignature(_rootSignatureShadowPass.Get());
		l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		auto shadowCB = _currFrameResource->ShadowCB->Resource();
		UINT shadowStride = D3DUtil::CalcConstantBufferSize(sizeof(ShadowConstant));

		_shadowMap->Render(l, [&](UINT k)
			{
				D3D12_GPU_VIRTUAL_ADDRESS cascadeCB = shadowCB->GetGPUVirtualAddress() + k * shadowStride;

				l->SetPipelineState(_psos["shadowPass"].Get());
				l->SetGraphicsRootSignature(_rootSignatureShadowPass.Get());
				l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
				l->SetGraphicsRootConstantBufferView(1, cascadeCB);

				DrawRenderItemsShadow(l, _shadowRitems);
				DrawTerrainShadow(l, cascadeCB);
			});
	}
	//GEOMETRY PASS (GBuffer)

	l->RSSetViewports(1, &_renderViewport);
	l->RSSetScissorRects(1, &_renderScissor);

	_gBuffer->TransitToOpaqueRenderingState(l);
	_gBuffer->ClearView(l);

	_velocityBuffer->TransitCurrentToWrite(l);
	_velocityBuffer->ClearCurrent(l);

	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, 4> rtvs =
	{
       _gBuffer->Textures[(UINT)GBufferIndex::Albedo].RTV.Cpu(),
       _gBuffer->Textures[(UINT)GBufferIndex::Normal].RTV.Cpu(),
       _gBuffer->Textures[(UINT)GBufferIndex::MatData].RTV.Cpu(),
	   _velocityBuffer->CurrentRTV().Cpu()
	};

	auto dsv = _gBuffer->Textures[(UINT)GBufferIndex::Depth].DSV.Cpu();
	l->OMSetRenderTargets((UINT)rtvs.size(), rtvs.data(), FALSE, &dsv);
	
	DrawRenderItems(l, _opaqueRitems);
	DrawTerrain(l);

	_gBuffer->TransitToLightRenderingState(l);
	
	_velocityBuffer->TransitCurrentToRead(l);

	//LIGHTING PASS

	auto toRT = CD3DX12_RESOURCE_BARRIER::Transition(_ppChain->GetHDRBuffer(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
	l->ResourceBarrier(1, &toRT);

	auto backBufferToRT = CD3DX12_RESOURCE_BARRIER::Transition(_device->BackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	l->ResourceBarrier(1, &backBufferToRT);

	float clearColor[4] = { 0, 0, 0, 1 };
	auto hdrRTV = _isDebugMode ? _device->BackBufferRtv().Cpu() : _ppChain->GetHDRRtv().Cpu();
	l->ClearRenderTargetView(hdrRTV, clearColor, 0, nullptr);

	D3D12_CPU_DESCRIPTOR_HANDLE depth = _gBuffer->Textures[(UINT)GBufferIndex::Depth].DSV.Cpu();
	l->OMSetRenderTargets(1, &hdrRTV, FALSE, &depth);

	l->SetPipelineState(_psos[_isDebugMode ? "lightPassDebug" : "lightPass"].Get());
	l->SetGraphicsRootSignature(_rootSignatureLightPass.Get());

	auto& gb = _gBuffer->Textures;
	l->SetGraphicsRootDescriptorTable(0, gb[(UINT)GBufferIndex::Albedo].SRV.Gpu());  // t0 Albedo
	l->SetGraphicsRootDescriptorTable(1, gb[(UINT)GBufferIndex::Normal].SRV.Gpu());  // t1 Normal
	l->SetGraphicsRootDescriptorTable(2, gb[(UINT)GBufferIndex::MatData].SRV.Gpu()); // t2 MatData
	l->SetGraphicsRootDescriptorTable(3, gb[(UINT)GBufferIndex::Depth].SRV.Gpu());   // t3 Depth

	l->SetGraphicsRootDescriptorTable(9, _iblSrv.Gpu(0));		                // t6 DefHDRI
	l->SetGraphicsRootDescriptorTable(10, _iblSrv.Gpu(1));		                // t7 SpecHDRI
	l->SetGraphicsRootDescriptorTable(11, _iblSrv.Gpu(2));                       // t8 TableHDRI
	l->SetGraphicsRootDescriptorTable(12, _velocityBuffer->Current().srv.Gpu()); // t9 Velocity

	l->SetGraphicsRootShaderResourceView(4, _currFrameResource->LightSB->Resource()->GetGPUVirtualAddress());	  // t4 LightSB
	l->SetGraphicsRootConstantBufferView(5, _currFrameResource->PassCB->Resource()->GetGPUVirtualAddress());		  // b0 PassCB
	l->SetGraphicsRootConstantBufferView(6, _currFrameResource->LightInfoCB->Resource()->GetGPUVirtualAddress());  // b1 LightInfoCB
	l->SetGraphicsRootConstantBufferView(7, _currFrameResource->ShadowCB->Resource()->GetGPUVirtualAddress());     // b2 ShadowCB

	l->SetGraphicsRootDescriptorTable(8, _shadowMap->SrvRange.Gpu());   // t5 ShadowMap

	// Fullscreen quad
	l->IASetVertexBuffers(0, 0, nullptr);
	l->IASetIndexBuffer(nullptr);
	l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	if (_isDebugMode)
	{
		// Debug пишет прямо в backbuffer (Display)
		l->RSSetViewports(1, &_screenViewport);

		std::vector<D3D12_RECT> scissorRects = BuildScissorRects(MAX_DEBUG_LAYER_COUNT, _renderSettings.Display);

		UINT passElementSize = D3DUtil::CalcConstantBufferSize(sizeof(PassConstants));

		for (int i = 0; i < MAX_DEBUG_LAYER_COUNT; i++)
		{
			PassConstants debugCB = _mainPassCB;
			debugCB.DebugMode = 1;
			debugCB.DebugViewIndex = i;

			_currFrameResource->PassCB->CopyData(i, debugCB);

			l->SetGraphicsRootConstantBufferView(5, _currFrameResource->PassCB->Resource()->GetGPUVirtualAddress() + i * passElementSize);
			l->RSSetScissorRects(1, &scissorRects[i]);
			l->DrawInstanced(3, 1, 0, 0);
		}
	}
	else
	{
		l->RSSetViewports(1, &_renderViewport);
		l->RSSetScissorRects(1, &_renderScissor);

		_mainPassCB.DebugMode = 0;
		_currFrameResource->PassCB->CopyData(0, _mainPassCB);

		l->SetGraphicsRootConstantBufferView(5, _currFrameResource->PassCB->Resource()->GetGPUVirtualAddress());

		l->DrawInstanced(3, 1, 0, 0);
	}

	auto toSRV = CD3DX12_RESOURCE_BARRIER::Transition(_ppChain->GetHDRBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	l->ResourceBarrier(1, &toSRV);

	// TAA PASS

	AAInputs in = {};
	in.color = { _ppChain->GetHDRBuffer(), _ppChain->GetHDRSrv() };
	in.depth = { _gBuffer->Textures[(UINT)GBufferIndex::Depth].Resource.Get(), _gBuffer->Textures[(UINT)GBufferIndex::Depth].SRV };
	in.prevVelocity = _velocityBuffer->Previous();
	in.velocity = _velocityBuffer->Current();

	_currentAA->Resolve(l, in);

	ID3D12DescriptorHeap* heapsAfterAA[] = { _device->Srv().Heap() };
	l->SetDescriptorHeaps(1, heapsAfterAA);

	////POST_PROCESS

	if (!_isDebugMode)
	{
		l->RSSetViewports(1, &_screenViewport);
		l->RSSetScissorRects(1, &_scissorRect);

		_ppChain->ExecuteAll(l, _currentAA->OutputSrv(), _device->BackBufferRtv());
	}

	//GUI

	auto backBuffer = _device->BackBufferRtv().Cpu();
	l->OMSetRenderTargets(1, &backBuffer, TRUE, nullptr);

	_gui->Render(l);

	auto backBufferToPresent = CD3DX12_RESOURCE_BARRIER::Transition(_device->BackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	l->ResourceBarrier(1, &backBufferToPresent);

	//PRESENT

	_velocityBuffer->Swap();
	_device->EndFrame();
}

void D3DFramework::OnKeyboardInput(const GameTimer& gt)
{
	float notches = _input.Wheel();

	if (notches != 0.0f && !_input.MouseCaptured)
	{
		bool ctrl = _input.IsDown(VK_CONTROL);
		bool shift = _input.IsDown(VK_SHIFT);

		if (_paintMode)
		{
			if (ctrl)
			{
				_brushRadius *= powf(1.15f, notches);
				_brushRadius = MathHelper::Clamp(_brushRadius, (float)BRUSH_RADIUS_MIN, (float)BRUSH_RADIUS_MAX);
			}
			else if (shift)
			{
				_brushStrength *= powf(1.15f, notches);
				_brushStrength = MathHelper::Clamp(_brushStrength, BRUSH_STRENGTH_MIN, BRUSH_STRENGTH_MAX);
			}
		}
		else
		{
			_moveSpeed *= powf(1.15f, notches);
			_moveSpeed = MathHelper::Clamp(_moveSpeed, 0.5f, 5000.0f);
		}
	}

	float dt = gt.DeltaTime();
	float speed = _moveSpeed * dt;

	Camera& cam = _world->MainCamera();
	XMVECTOR pos = XMLoadFloat3(&cam.Position);
	XMVECTOR forward = cam.Forward();
	XMVECTOR right = cam.Right();

	if (_input.IsDown('W')) { pos += speed * forward; }

	if (_input.IsDown('S')) { pos -= speed * forward; }

	if (_input.IsDown('A')) { pos -= speed * right; }

	if (_input.IsDown('D')) { pos += speed * right; }

	if (_input.WasPressed(VK_F1))
	{
		_isDebugMode = !_isDebugMode;
		Sleep(200);
	}

	if (_input.WasPressed(VK_F2))
	{
		_isUseBeckmann = !_isUseBeckmann;
		Sleep(200);
	}

	if (_input.WasPressed(VK_F3))
	{
		_isTerrainDegub = !_isTerrainDegub;
		Sleep(200);
	}

	if (_input.WasPressed(VK_F4))
	{
		_paintMode = !_paintMode;
		Sleep(200);
	}

	if (_input.WasPressed(VK_F5))
	{
		_renderSettings.Mode = (AAMode)(((int)_renderSettings.Mode + 1) % 3);

		_currentAA = GetAAByMode(_renderSettings.Mode);
		_renderSettingsDirty = true;

		Sleep(200);
	}

	if (_input.WasPressed(VK_F6))
	{
		int next = ((int)_renderSettings.Preset + 1) % (int)UpscalePreset::Custom;
		_renderSettings.Preset = (UpscalePreset)next;
		_renderSettingsDirty = true;
		Sleep(200);
	}

	if (_input.WasPressed(VK_F7))
	{
		_renderSettings.Preset = UpscalePreset::Custom;
		_renderSettings.RenderScale = std::max(0.33f, _renderSettings.RenderScale - 0.05f);
		_renderSettingsDirty = true;

		Sleep(200);
	}

	if (_input.WasPressed(VK_F8))
	{
		_renderSettings.Preset = UpscalePreset::Custom;
		_renderSettings.RenderScale = std::min(1.0f, _renderSettings.RenderScale + 0.05f);
		_renderSettingsDirty = true;

		Sleep(200);
	}

	for (int vk = '1'; vk <= '9'; vk++)
	{
		if (_input.WasPressed(vk))
		{
			UINT layer = vk - '1';
			if (layer < _terrain->GetLayerCount()) { _brushLayer = layer; }
		}
	}

	if (_input.WasPressed(VK_OEM_PLUS) && _input.WasPressed(VK_CONTROL)) { _brushMode = BrushMode::Draw; }
	if (_input.WasPressed(VK_OEM_MINUS) && _input.WasPressed(VK_CONTROL)) { _brushMode = BrushMode::Erase; }

	XMStoreFloat3(&cam.Position, pos);
}

void D3DFramework::UpdateCamera(const GameTimer& gt)
{
	Camera& cam = _world->MainCamera();

	if (!_paintMode && !_input.MouseCaptured && _input.IsDown(VK_LBUTTON))
	{
		XMFLOAT2 d = _input.MouseDelta();

		cam.Yaw += XMConvertToRadians(_rotateSpeed * d.x);
		cam.Pitch -= XMConvertToRadians(_rotateSpeed * d.y);
		cam.Pitch = MathHelper::Clamp(cam.Pitch, -XM_PIDIV2 + 0.1f, XM_PIDIV2 - 0.1f);
	}
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

	_assets->ForEachMaterial([&](MaterialHandle h, Material& m)
		{
			if (m.NumFramesDirty <= 0) { return; }

			MaterialConstants c;
			c.DiffuseAlbedo = m.DiffuseAlbedo;
			c.Roughness = m.Roughness;
			c.Metallic = m.Metallic;
			c.NormalIntencity = m.NormalIntensity;

			currMaterialCB->CopyData(h.Index, c);
			m.NumFramesDirty--;
		});
}

void D3DFramework::UpdateMainPassCB(const GameTimer& gt)
{
	Camera& cam = _world->MainCamera();

	_camFrustum = cam.Frustum(AspectRatio());

	XMMATRIX view = cam.View();
	XMMATRIX proj = cam.Proj(AspectRatio());

	XMMATRIX invView = XMMatrixInverse(nullptr, view);
	XMMATRIX invProj = XMMatrixInverse(nullptr, proj);

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	XMMATRIX jitteredViewProj = viewProj;
	if (_currentAA->UsesJitter())
	{
		XMFLOAT2 jitter = _currentAA->NextJitterPx();
		jitteredViewProj = XMMatrixMultiply(viewProj, XMMatrixTranslation(jitter.x, jitter.y, 0.0f));
	}

	XMMATRIX invViewProj = XMMatrixInverse(nullptr, jitteredViewProj);

	XMStoreFloat4x4(&_mainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&_mainPassCB.InvView, XMMatrixTranspose(invView));

	XMStoreFloat4x4(&_mainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&_mainPassCB.InvProj, XMMatrixTranspose(invProj));

	_mainPassCB.PrevViewProj = _mainPassCB.ViewProj;
	XMStoreFloat4x4(&_mainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&_mainPassCB.JitteredViewProj, XMMatrixTranspose(jitteredViewProj));
	XMStoreFloat4x4(&_mainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));

	_mainPassCB.EyePosW = cam.Position;
	_mainPassCB.RenderTargetSize = _renderSettings.Render.ToFloat2();
	_mainPassCB.InvRenderTargetSize = _renderSettings.Render.InvFloat2();
	_mainPassCB.NearZ = 1.0f;
	_mainPassCB.FarZ = 1000.0f;
	_mainPassCB.TotalTime = gt.TotalTime();
	_mainPassCB.DeltaTime = gt.DeltaTime();
	_mainPassCB.DebugMode = _isDebugMode ? 1 : 0;
	_mainPassCB.DebugUseBeckmann = _isUseBeckmann ? 1 : 0;
	_mainPassCB.TerrainDebug = _isTerrainDegub ? 1 : 0;

	auto currPassCB = _currFrameResource->PassCB.get();
	currPassCB->CopyData(0, _mainPassCB);

	TessellationConstant tess = TessellationConstant();
	auto tessCB = _currFrameResource->TessellationCB.get();
	tessCB->CopyData(0, tess);

	DisplacementConstant disp = DisplacementConstant();
	auto dispCB = _currFrameResource->DisplacementCB.get();
	dispCB->CopyData(0, disp);
}

void D3DFramework::UpdateLightSB(const GameTimer& gt)
{
	auto* sb = _currFrameResource->LightSB.get();
	UINT count = 0;

	for (const LightObject& l : _world->Lights())
	{
		if (!l.Active) { continue; }

		LightConstants c = {};
		c.Strength = l.Color;
		c.FalloffStart = l.FalloffStart;
		c.Direction = l.Direction;
		c.FalloffEnd = l.FalloffEnd;
		c.Position = l.Position;
		c.SpotPower = l.SpotPower;
		c.LightType = (int)l.Type;

		sb->CopyData(count++, c);
	}

	LightInfoConstants info = {};
	info.LightCount = count;
	info.IBLIntensity = 1.0f;
	_currFrameResource->LightInfoCB->CopyData(0, info);
}

void D3DFramework::UpdateShadowCB(const GameTimer& gt)
{
	const float cascadeNears[] = { 1.0f,  50.0f,  200.0f };
	const float cascadeFars[] = { 50.0f, 200.0f, 1000.0f };

	XMMATRIX view = _world->MainCamera().View();

	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f
	);

	auto currShadowCB = _currFrameResource->ShadowCB.get();

	for (const auto& light : _world->Lights())
	{
		if (light.Type != LightType::Directional) { continue; }

		XMVECTOR lightDir = XMVector3Normalize(XMLoadFloat3(&light.Direction));

		XMVECTOR up = XMVectorSet(0, 1, 0, 0);
		if (fabsf(XMVectorGetY(lightDir)) > 0.99f) { up = XMVectorSet(1, 0, 0, 0); }

		for (UINT cascade = 0; cascade < _shadowSettings.CascadeCount; cascade++)
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

			BoundingBox sceneBox = _world->Bounds();
			BoundingBox terrainBox(XMFLOAT3(0.0f, TERRAIN_HEIGHT_OFFSET + TERRAIN_HEIGHT_SCALE * 0.5f, 0.0f), XMFLOAT3(TERRAIN_WORLD_SIZE * 0.5f, TERRAIN_HEIGHT_SCALE * 0.5f, TERRAIN_WORLD_SIZE * 0.5f));
			BoundingBox::CreateMerged(sceneBox, sceneBox, terrainBox);

			XMFLOAT3 sceneCorners[8];
			sceneBox.GetCorners(sceneCorners);

			for (const XMFLOAT3& c : sceneCorners)
			{
				float vz = XMVectorGetZ(XMVector3TransformCoord(XMLoadFloat3(&c), lightView));
				minZ = std::min(minZ, vz);
				maxZ = std::max(maxZ, vz);
			}

			float worldTexelSizeX = (maxX - minX) / _shadowSettings.ShadowTextureSize;
			float worldTexelSizeY = (maxY - minY) / _shadowSettings.ShadowTextureSize;
			minX = floorf(minX / worldTexelSizeX) * worldTexelSizeX;
			maxX = floorf(maxX / worldTexelSizeX) * worldTexelSizeX;
			minY = floorf(minY / worldTexelSizeY) * worldTexelSizeY;
			maxY = floorf(maxY / worldTexelSizeY) * worldTexelSizeY;

			XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(minX, maxX, minY, maxY, minZ, maxZ);

			XMMATRIX lightViewProj = XMMatrixMultiply(lightView, lightProj);

			XMVECTOR originNDC = XMVector3TransformCoord(XMVectorZero(), lightViewProj);
			float texelSizeNDC = 2.0f / _shadowSettings.ShadowTextureSize;
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

void D3DFramework::UpdateTerrainCB(const GameTimer& gt)
{
	const Camera& cam = _world->MainCamera();
	const float screenH = (float)_renderSettings.Display.Height;

	_terrain->UpdateLOD(cam.Position, cam.FovY, screenH, _camFrustum);
	_terrain->UpdateShadowLOD(cam.Position, cam.FovY, screenH);

	assert(_terrain->GetVisibleTiles().size() + _terrain->GetShadowTiles().size() <= MAX_TERRAIN_TILES);

	TerrainConstant terrainBase = TerrainConstant();
	terrainBase.MaskCount = _terrain->GetMaskCount();
	terrainBase.LayerCount = _terrain->GetLayerCount();
	terrainBase.BrushMode = (_brushMode == BrushMode::Erase) ? 1.0f : 0.0f;
	terrainBase.BrushStrength = logf(_brushStrength / BRUSH_STRENGTH_MIN) / logf(BRUSH_STRENGTH_MAX / BRUSH_STRENGTH_MIN);
	terrainBase.BrushRadius = _paintMode ? _brushRadius : 0.0f;
	const auto& layerTileSizes = _terrain->GetLayerTileSizes();

	float* layerTileDst = &terrainBase.LayerTile[0].x;
	for (size_t k = 0; k < layerTileSizes.size(); k++) { layerTileDst[k] = layerTileSizes[k]; }

	auto* cb = _currFrameResource->TerrainCB.get();
	UINT i = 0;

	auto writeTile = [&](const TerrainTile& t)
		{
			TerrainConstant c = terrainBase;
			c.TileOrigin = t.Origin;
			c.TileSize = t.Size;
			c.MorphStart = t.MorphStart;
			c.MorphEnd = t.MorphEnd;
			c.LodLevel = t.Depth;
			cb->CopyData(i++, c);
		};

	for (const auto& t : _terrain->GetVisibleTiles()) { writeTile(t); }
	_terrainShadowCBOffset = i;
	for (const auto& t : _terrain->GetShadowTiles()) { writeTile(t); }
}

void D3DFramework::UpdateInstanceData(const GameTimer& gt)
{
	if (_world->Version() != _builtWorldVersion) { BuildRenderItems(); }

	_opaqueRitems.clear();
	_shadowRitems.clear();

	auto* sb = _currFrameResource->InstanceDataSB.get();
	UINT offset = 0;

	auto write = [&](const MeshObject& o)
		{
			InstanceData d;
			XMStoreFloat4x4(&d.World, XMMatrixTranspose(XMLoadFloat4x4(&o.World)));
			XMStoreFloat4x4(&d.PrevWorld, XMMatrixTranspose(XMLoadFloat4x4(&o.PrevWorld)));
			XMStoreFloat4x4(&d.TexTransform, XMMatrixIdentity());
			sb->CopyData(offset++, d);
		};

	for (auto& ri : _allRitems) { ri->Scratch.clear(); }

	_world->ForEachObject([&](ObjectId, MeshObject& o)
		{
			for (RenderItem* ri : _modelBatches[o.Model]) { ri->Scratch.push_back(&o); }
		});

	for (auto& ri : _allRitems)
	{
		ri->ShadowInstanceOffset = offset;
		ri->ShadowInstanceCount = (UINT)ri->Scratch.size();
		for (const MeshObject* o : ri->Scratch) { write(*o); }
		if (ri->ShadowInstanceCount > 0) { _shadowRitems.push_back(ri.get()); }
		ri->Scratch.clear();
	}

	_world->QueryVisible(_camFrustum, _visibleObjects);
	_frameStats.VisibleInstances = (UINT)_visibleObjects.size();
	_frameStats.TotalInstances = (UINT)_world->ObjectCount();

	for (ObjectId id : _visibleObjects)
	{
		const MeshObject* o = _world->Get(id);
		if (!o) { continue; }
		for (RenderItem* ri : _modelBatches[o->Model]) { ri->Scratch.push_back(o); }
	}

	for (auto& ri : _allRitems)
	{
		ri->InstanceOffset = offset;
		ri->VisibleInstanceCount = (UINT)ri->Scratch.size();
		for (const MeshObject* o : ri->Scratch) { write(*o); }
		if (ri->VisibleInstanceCount > 0) { _opaqueRitems.push_back(ri.get()); }
	}

	assert(offset <= 10000 && "InstanceDataSB overflow");
}

void D3DFramework::BuildRootSignatureGBuffer()
{
	CD3DX12_DESCRIPTOR_RANGE albedoRange; 
	albedoRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE normalRange; 
	normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_DESCRIPTOR_RANGE metallicRange;
	metallicRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

	CD3DX12_DESCRIPTOR_RANGE roughnessRange;
	roughnessRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

	CD3DX12_DESCRIPTOR_RANGE aoRange;
	aoRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);

	CD3DX12_DESCRIPTOR_RANGE dispRange;
	dispRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);

	CD3DX12_ROOT_PARAMETER slotRootParameter[11];
	slotRootParameter[0].InitAsDescriptorTable(1, &albedoRange, D3D12_SHADER_VISIBILITY_ALL);    // t0 albedo
	slotRootParameter[1].InitAsDescriptorTable(1, &normalRange, D3D12_SHADER_VISIBILITY_ALL);    // t1 normal
	slotRootParameter[2].InitAsDescriptorTable(1, &metallicRange, D3D12_SHADER_VISIBILITY_ALL);  // t2 metallic
	slotRootParameter[3].InitAsDescriptorTable(1, &roughnessRange, D3D12_SHADER_VISIBILITY_ALL); // t3 roughness
	slotRootParameter[4].InitAsDescriptorTable(1, &aoRange, D3D12_SHADER_VISIBILITY_ALL);        // t4 ao
	slotRootParameter[5].InitAsDescriptorTable(1, &dispRange, D3D12_SHADER_VISIBILITY_ALL);      // t5 displacement

	slotRootParameter[6].InitAsConstantBufferView(0);                                            // b0 cbPass
	slotRootParameter[7].InitAsConstantBufferView(1);                                            // b1 cbMaterial
	slotRootParameter[8].InitAsConstantBufferView(2);                                            // b2 cbTessellation
	slotRootParameter[9].InitAsConstantBufferView(3);                                            // b3 cbDisplacement

	slotRootParameter[10].InitAsShaderResourceView(3, 1, D3D12_SHADER_VISIBILITY_ALL);           // t3 space1 instances

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(11, slotRootParameter, (UINT)staticSamplers.size(), staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(_device->Device()->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignatureGBuffer)
	));
}

void D3DFramework::BuildRootSignatureLightPass()
{
	CD3DX12_DESCRIPTOR_RANGE albedoRange; 
	albedoRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE normalRange;
	normalRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

	CD3DX12_DESCRIPTOR_RANGE matDataRange;
	matDataRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);

	CD3DX12_DESCRIPTOR_RANGE depthRange;
	depthRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

	CD3DX12_DESCRIPTOR_RANGE shadowRange;
	shadowRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);

	CD3DX12_DESCRIPTOR_RANGE irradianceRange;
	irradianceRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);

	CD3DX12_DESCRIPTOR_RANGE specularRange;
	specularRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7);

	CD3DX12_DESCRIPTOR_RANGE brdfRange;
	brdfRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8);

	CD3DX12_DESCRIPTOR_RANGE velocityRange;
	velocityRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 9);

	CD3DX12_ROOT_PARAMETER slotRootParameter[13];
	slotRootParameter[0].InitAsDescriptorTable(1, &albedoRange);	 // t0 Albedo
	slotRootParameter[1].InitAsDescriptorTable(1, &normalRange);	 // t1 Normal
	slotRootParameter[2].InitAsDescriptorTable(1, &matDataRange);	 // t2 MatData
	slotRootParameter[3].InitAsDescriptorTable(1, &depthRange);		 // t3 Depth
	slotRootParameter[4].InitAsShaderResourceView(4);				 // t4 LightSB
	slotRootParameter[5].InitAsConstantBufferView(0);				 // b0 PassCB
	slotRootParameter[6].InitAsConstantBufferView(1);				 // b1 LightInfoCB
	slotRootParameter[7].InitAsConstantBufferView(2);				 // b2 ShadowCB
	slotRootParameter[8].InitAsDescriptorTable(1, &shadowRange);	 // t5 ShadowMap
	slotRootParameter[9].InitAsDescriptorTable(1, &irradianceRange); // t6 DiffuseHDR
	slotRootParameter[10].InitAsDescriptorTable(1, &specularRange);  // t7 SpecularHDR
	slotRootParameter[11].InitAsDescriptorTable(1, &brdfRange);      // t8 Brdf
	slotRootParameter[12].InitAsDescriptorTable(1, &velocityRange);	 // t9 velocity

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(13, slotRootParameter, (UINT)staticSamplers.size(), staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_NONE);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());

	ThrowIfFailed(hr);
	ThrowIfFailed(_device->Device()->CreateRootSignature(0,
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

	ThrowIfFailed(_device->Device()->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignatureShadowPass)
	));
}

void D3DFramework::BuildRootSignatureTerrainPass()
{
	CD3DX12_DESCRIPTOR_RANGE texRange;
	texRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0);   // 3 SRV

	CD3DX12_DESCRIPTOR_RANGE hitRange;
	hitRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3);

	CD3DX12_ROOT_PARAMETER slotRootParameter[5];
	slotRootParameter[0].InitAsDescriptorTable(1, &texRange, D3D12_SHADER_VISIBILITY_ALL); // t0, t1, t3
	slotRootParameter[1].InitAsConstantBufferView(0);   // b0 cbPass
	slotRootParameter[2].InitAsConstantBufferView(4);   // b4 cbTerrain
	slotRootParameter[3].InitAsDescriptorTable(1, &hitRange, D3D12_SHADER_VISIBILITY_PIXEL); //hit
	slotRootParameter[4].InitAsConstantBufferView(1);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(5, slotRootParameter, (UINT)staticSamplers.size(), staticSamplers.data(), D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1, serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}

	ThrowIfFailed(hr);

	ThrowIfFailed(_device->Device()->CreateRootSignature(0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(&_rootSignatureTerrainPass)
	));
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
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	psoDesc.NumRenderTargets = 4;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;       // Albedo
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;   // Normal
	psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;       // MatData
	psoDesc.RTVFormats[3] = DXGI_FORMAT_R16G16_FLOAT;         // Velocity 

	psoDesc.DSVFormat = GBuffer::DEPTH_DSV_FORMAT;

	ThrowIfFailed(_device->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["gbuffer"])));
}

void D3DFramework::BuildLightPassPSO()
{
	CD3DX12_BLEND_DESC blendDesc(D3D12_DEFAULT);

	CD3DX12_DEPTH_STENCIL_DESC dsDesc(D3D12_DEFAULT);
	dsDesc.DepthEnable = FALSE;
	dsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	dsDesc.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;

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
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	psoDesc.RTVFormats[0] = HDR_FORMAT;
	psoDesc.DSVFormat = GBuffer::DEPTH_DSV_FORMAT;

	ThrowIfFailed(_device->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["lightPass"])));
	
	psoDesc.RTVFormats[0] = _device->BackBufferFormat();
	ThrowIfFailed(_device->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["lightPassDebug"])));
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
	psoDesc.RasterizerState.DepthClipEnable = FALSE;

	psoDesc.NumRenderTargets = 0;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	psoDesc.RTVFormats[0] = DXGI_FORMAT_UNKNOWN;
	psoDesc.DSVFormat = ShadowMap::DEPTH_DSV_FORMAT;

	ThrowIfFailed(_device->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["shadowPass"])));
}

void D3DFramework::BuildTerrainPassPSO()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
	ZeroMemory(&psoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));

	psoDesc.InputLayout = { _inputLayout.data(), (UINT)_inputLayout.size() };
	psoDesc.pRootSignature = _rootSignatureTerrainPass.Get();

	psoDesc.VS =
	{
		reinterpret_cast<BYTE*>(_shaders["terrainVS"]->GetBufferPointer()),
		_shaders["terrainVS"]->GetBufferSize()
	};
	psoDesc.PS =
	{
		reinterpret_cast<BYTE*>(_shaders["terrainPS"]->GetBufferPointer()),
		_shaders["terrainPS"]->GetBufferSize()
	};

	psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

	psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	psoDesc.SampleMask = UINT_MAX;
	psoDesc.SampleDesc.Count = 1;
	psoDesc.SampleDesc.Quality = 0;

	psoDesc.NumRenderTargets = 4;
	psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;       // Albedo
	psoDesc.RTVFormats[1] = DXGI_FORMAT_R32G32B32A32_FLOAT;   // Normal
	psoDesc.RTVFormats[2] = DXGI_FORMAT_R8G8B8A8_UNORM;       // MatData
	psoDesc.RTVFormats[3] = DXGI_FORMAT_R16G16_FLOAT;         // Velocity 

	psoDesc.DSVFormat = GBuffer::DEPTH_DSV_FORMAT;

	ThrowIfFailed(_device->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["terrain"])));

	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(_device->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["terrainWire"])));

	psoDesc.VS =
	{ 
		reinterpret_cast<BYTE*>(_shaders["terrainShadowVS"]->GetBufferPointer()),
		_shaders["terrainShadowVS"]->GetBufferSize()
	};
	psoDesc.PS = { nullptr, 0 };

	psoDesc.NumRenderTargets = 0;
	for (int i = 0; i < 8; i++) { psoDesc.RTVFormats[i] = DXGI_FORMAT_UNKNOWN; }

	psoDesc.DSVFormat = ShadowMap::DEPTH_DSV_FORMAT;
	psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	psoDesc.RasterizerState.DepthBias = 5000;
	psoDesc.RasterizerState.SlopeScaledDepthBias = 1.5f;
	psoDesc.RasterizerState.DepthClipEnable = FALSE;
	psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(_device->Device()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&_psos["terrainShadow"])));
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

	_shaders["terrainVS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/" + TERRAIN_FOLDER_W + L"Terrain.hlsl", nullptr, "VS", "vs_5_1");
	_shaders["terrainShadowVS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/" + TERRAIN_FOLDER_W + L"Terrain.hlsl", nullptr, "VS_Shadow", "vs_5_1");
	_shaders["terrainPS"] = D3DUtil::CompileShader(LOCAL_PATH_W + L"Shaders/" + TERRAIN_FOLDER_W + L"Terrain.hlsl", nullptr, "PS", "ps_5_1");

	_inputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};
}

void D3DFramework::CreatePPS()
{
	PassDesc toneDesc = {};
	toneDesc.Name = "ToneMapping";
	toneDesc.ShaderPath = LOCAL_PATH + "Shaders/" + POST_PROCESS_FOLDER + "ToneMapping.hlsl";
	toneDesc.Stage = PostProcessStage::Tonemapping;
	toneDesc.OutputFormat = _device->BackBufferFormat();
	toneDesc.Res = _renderSettings.Display;

	_ppChain->ReservePass(*_device, toneDesc);

	_ppChain->InitHDRBuffer(*_device, _renderSettings.Render);

	_ppChain->CommitAll(_device->Device());
}

//void D3DFramework::CreatePPS()
//{
//	PassDesc cellDesc = {};
//	cellDesc.Name = "CellShader";
//	cellDesc.ShaderPath = LOCAL_PATH + "Shaders/" + POST_PROCESS_FOLDER + "CellShader.hlsl";
//	cellDesc.Stage = PostProcessStage::BeforeTonemapping;
//	cellDesc.Priority = 0;
//	cellDesc.OutputFormat = HDR_FORMAT;
//	cellDesc.Res = _renderSettings.Display;
//	cellDesc.ExtraInputSrv = { _gBuffer->Textures[(UINT)GBufferIndex::Albedo].SRV };
//
//	struct OutlineConstants
//	{
//		DirectX::XMFLOAT2 TexelSize;
//		float KernelSize = 10.0f;
//		float pad0 = 0.0f;
//		DirectX::XMFLOAT2 DepthThreshold = { 2.0f, 10.0f };
//		DirectX::XMFLOAT2 NormalThreshold = { 0.15f, 0.2f };
//		DirectX::XMFLOAT3 OutlineColor = { 0.0f, 0.0f, 0.0f };
//		float pad1 = 0.0f;
//	};
//
//	OutlineConstants outlineCB = {};
//	outlineCB.TexelSize = _renderSettings.Display.InvFloat2();
//
//	PassDesc outlineDesc = {};
//	outlineDesc.Name = "Outline";
//	outlineDesc.ShaderPath = LOCAL_PATH + "Shaders/" + POST_PROCESS_FOLDER + "Outline.hlsl";
//	outlineDesc.Stage = PostProcessStage::BeforeTonemapping;
//	outlineDesc.Priority = 1;
//	outlineDesc.OutputFormat = HDR_FORMAT;
//	outlineDesc.Res = _renderSettings.Display;
//	outlineDesc.HasConstantBuffer = true;
//	outlineDesc.ConstantBufferSize = sizeof(OutlineConstants);
//	outlineDesc.ConstantBufferData = &outlineCB;
//	outlineDesc.ExtraInputSrv =
//	{
//		_gBuffer->Textures[(UINT)GBufferIndex::Depth].SRV,
//		_gBuffer->Textures[(UINT)GBufferIndex::Normal].SRV
//	};
//
//	PassDesc toneDesc = {};
//	toneDesc.Name = "ToneMapping";
//	toneDesc.ShaderPath = LOCAL_PATH + "Shaders/" + POST_PROCESS_FOLDER + "ToneMapping.hlsl";
//	toneDesc.Stage = PostProcessStage::Tonemapping;
//	toneDesc.OutputFormat = _backBufferFormat;
//	toneDesc.Res = _renderSettings.Display;
//
//	_ppChain->ReservePass(*_device, cellDesc);
//	_ppChain->ReservePass(*_device, outlineDesc);
//	_ppChain->ReservePass(*_device, toneDesc);
//
//	_ppChain->InitHDRBuffer(*_device, _renderSettings.Display);
//
//	_ppChain->CommitAll(_device->Device());
//}

void D3DFramework::BuildFrameResources()
{
	int dirLightCount = 0;
	for (const auto& light : _world->Lights())
	{
		if (light.Type == LightType::Directional)
		{
			dirLightCount++;
		}
	}

	for (UINT i = 0; i < GraphicsDevice::FRAME_COUNT; ++i)
	{
		_frameResources.push_back(std::make_unique<FrameResource>(_device->Device(), (UINT)MAX_DEBUG_LAYER_COUNT, AssetManager::MAX_MATERIALS, std::max<UINT>(1, (UINT)_world->Lights().size()), std::max<UINT>(1, dirLightCount) * _shadowSettings.CascadeCount));
	}
}

void D3DFramework::BuildRenderItems()
{
	_allRitems.clear();
	_opaqueRitems.clear();

	std::map<std::tuple<const MeshGeometry*, uint32_t, uint32_t, uint32_t>, RenderItem*> byKey;

	_world->ForEachObject([&](ObjectId, MeshObject& obj)
		{
			if (_modelBatches.count(obj.Model)) { return; }

			const Model* model = _assets->GetModel(obj.Model);
			if (!model || !model->Mesh) { return; }

			std::vector<RenderItem*>& batches = _modelBatches[obj.Model];

			for (const Model::Part& part : model->Parts)
			{
				auto key = std::make_tuple(model->Mesh.get(), part.Submesh, part.Material.Index, part.Material.Generation);

				auto it = byKey.find(key);
				if (it == byKey.end())
				{
					auto ri = std::make_unique<RenderItem>();
					const Submesh& sub = model->Mesh->Submeshes[part.Submesh];

					ri->Geo = model->Mesh.get();
					ri->SubmeshIndex = part.Submesh;
					ri->Material = part.Material;
					ri->IndexCount = sub.IndexCount;
					ri->StartIndexLocation = sub.StartIndexLocation;
					ri->BaseVertexLocation = sub.BaseVertexLocation;
					ri->UsedPso = "opaque";

					it = byKey.emplace(key, ri.get()).first;
					_allRitems.push_back(std::move(ri));
				}

				batches.push_back(it->second);
			}
		});

	_builtWorldVersion = _world->Version();
}

void D3DFramework::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	cmdList->SetPipelineState(_psos["gbuffer"].Get());
	cmdList->SetGraphicsRootSignature(_rootSignatureGBuffer.Get());

	auto passCB = _currFrameResource->PassCB->Resource();
	auto tessCB = _currFrameResource->TessellationCB->Resource();
	auto dispCB = _currFrameResource->DisplacementCB->Resource();

	cmdList->SetGraphicsRootConstantBufferView(6, passCB->GetGPUVirtualAddress());  // b0
	cmdList->SetGraphicsRootConstantBufferView(8, tessCB->GetGPUVirtualAddress());  // b2
	cmdList->SetGraphicsRootConstantBufferView(9, dispCB->GetGPUVirtualAddress());  // b3

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

	auto matCB = _currFrameResource->MaterialCB->Resource();
	UINT matCBByteSize = D3DUtil::CalcConstantBufferSize(sizeof(MaterialConstants));

	auto instanceBuffer = _currFrameResource->InstanceDataSB->Resource();
	UINT instanceByteSize = sizeof(InstanceData);

	for (RenderItem* ri : ritems)
	{
		if (!ri || ri->VisibleInstanceCount == 0) { continue; }

		const Material* mat = _assets->GetMaterial(ri->Material);
		if (!mat) { continue; }

		auto vertexBuffer = ri->Geo->VertexBufferView();
		cmdList->IASetVertexBuffers(0, 1, &vertexBuffer);

		auto indexBuffer = ri->Geo->IndexBufferView();
		cmdList->IASetIndexBuffer(&indexBuffer);

		cmdList->SetGraphicsRootDescriptorTable(0, _assets->Textures().Srv(mat->AlbedoMap, DefaultTex::Albedo));                  // t0 albedo
		cmdList->SetGraphicsRootDescriptorTable(1, _assets->Textures().Srv(mat->NormalMap, DefaultTex::Normal));                  // t1 normal
		cmdList->SetGraphicsRootDescriptorTable(2, _assets->Textures().Srv(mat->MetallicMap, DefaultTex::Metallic));              // t2 metallic
		cmdList->SetGraphicsRootDescriptorTable(3, _assets->Textures().Srv(mat->RoughnessMap, DefaultTex::Roughness));            // t3 roughness
		cmdList->SetGraphicsRootDescriptorTable(4, _assets->Textures().Srv(mat->AOMap, DefaultTex::AO));                          // t4 ao
		cmdList->SetGraphicsRootDescriptorTable(5, _assets->Textures().Srv(mat->DisplacementMap, DefaultTex::Displacement));      // t5 displacement

		D3D12_GPU_VIRTUAL_ADDRESS matCBAddr = matCB->GetGPUVirtualAddress() + ri->Material.Index * matCBByteSize;
		cmdList->SetGraphicsRootConstantBufferView(7, matCBAddr);    // b1 cbMaterial

		D3D12_GPU_VIRTUAL_ADDRESS instAddr = instanceBuffer->GetGPUVirtualAddress() + ri->InstanceOffset * instanceByteSize;
		cmdList->SetGraphicsRootShaderResourceView(10, instAddr);    // t3 space1

		cmdList->DrawIndexedInstanced(ri->IndexCount, ri->VisibleInstanceCount, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
		
		_frameStats.DrawCalls++;
		_frameStats.Triangles += ri->IndexCount / 3 * ri->VisibleInstanceCount;
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
	
		_frameStats.ShadowDraws++;
	}
}

void D3DFramework::DrawTerrain(ID3D12GraphicsCommandList* cmdList)
{
	cmdList->SetPipelineState(_psos[_isTerrainDegub ? "terrainWire" : "terrain"].Get());
	cmdList->SetGraphicsRootSignature(_rootSignatureTerrainPass.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList->SetGraphicsRootDescriptorTable(0, _terrain->TexturesGpu());
	cmdList->SetGraphicsRootDescriptorTable(3, _terrain->HitSrvGpu());

	auto passCB = _currFrameResource->PassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress()); // b0

	UINT tileCbSize = D3DUtil::CalcConstantBufferSize(sizeof(TerrainConstant));
	auto terrainCB = _currFrameResource->TerrainCB->Resource();

	const MeshGeometry* mesh = _terrain->GetTileMesh();

	auto vbv = mesh->VertexBufferView();
	auto ibv = mesh->IndexBufferView();
	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetIndexBuffer(&ibv);

	for (UINT tileIndex = 0; tileIndex < _terrain->GetVisibleTiles().size(); tileIndex++)
	{
		cmdList->SetGraphicsRootConstantBufferView(2, terrainCB->GetGPUVirtualAddress() + tileIndex * tileCbSize); // b4

		cmdList->DrawIndexedInstanced(mesh->Submeshes[0].IndexCount, 1, 0, 0, 0);

		_frameStats.TerrainTiles++;
	}
}

void D3DFramework::DrawTerrainShadow(ID3D12GraphicsCommandList* cmdList, const D3D12_GPU_VIRTUAL_ADDRESS& cascadeCB)
{
	cmdList->SetPipelineState(_psos["terrainShadow"].Get());
	cmdList->SetGraphicsRootSignature(_rootSignatureTerrainPass.Get());
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	cmdList->SetGraphicsRootDescriptorTable(0, _terrain->TexturesGpu());
	cmdList->SetGraphicsRootConstantBufferView(4, cascadeCB);

	auto passCB = _currFrameResource->PassCB->Resource();
	cmdList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress()); // b0

	UINT tileCbSize = D3DUtil::CalcConstantBufferSize(sizeof(TerrainConstant));
	auto terrainCB = _currFrameResource->TerrainCB->Resource();

	const MeshGeometry* mesh = _terrain->GetTileMesh();

	auto vbv = mesh->VertexBufferView();
	auto ibv = mesh->IndexBufferView();
	cmdList->IASetVertexBuffers(0, 1, &vbv);
	cmdList->IASetIndexBuffer(&ibv);

	const auto& tiles = _terrain->GetShadowTiles();
	for (UINT t = 0; t < (UINT)tiles.size(); t++)
	{
		UINT cbIndex = _terrainShadowCBOffset + t;
		cmdList->SetGraphicsRootConstantBufferView(2, terrainCB->GetGPUVirtualAddress() + cbIndex * tileCbSize);
		cmdList->DrawIndexedInstanced(mesh->Submeshes[0].IndexCount, 1, 0, 0, 0);

		_frameStats.ShadowDraws++;
	}
}

void D3DFramework::LoadIBLTextures()
{
	std::vector<ComPtr<ID3D12Resource>> uploadHeaps;
	uploadHeaps.resize(3);

	_iblSrv = _device->Srv().Allocate(3);

	auto LoadDDS = [&](const std::wstring& path, ComPtr<ID3D12Resource>& resource, bool isCubemap, int idx)
	{
		ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(_device->Device(), _device->CommandList(), path.c_str(), resource, uploadHeaps[idx]));

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

		_device->Device()->CreateShaderResourceView(resource.Get(), &srvDesc, _iblSrv.Cpu(idx));
	};

	std::wstring hdriPath = LOCAL_PATH_W + L"HDRI/";

	LoadDDS(hdriPath + L"DiffuseHDR.dds", _iblIrradiance, true, 0);
	LoadDDS(hdriPath + L"SpecularHDR.dds", _iblSpecular, true, 1);
	LoadDDS(hdriPath + L"Brdf.dds", _iblBrdf, false, 2);

	for (auto& upload : uploadHeaps)
	{
		_device->Release().Push(upload);
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

AntiAliasing* D3DFramework::GetAAByMode(AAMode mode)
{
	switch (mode)
	{
	case AAMode::None:
		return static_cast<AntiAliasing*>(_noAA.get());
	case AAMode::TAA:
		return static_cast<AntiAliasing*>(_TAA.get());
	case AAMode::DLAA:
		return static_cast<AntiAliasing*>(_DLAA.get());
	default:
		return nullptr;
	}
}
