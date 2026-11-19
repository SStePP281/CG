#include "BufferVisualizer.h"
#include "ThrowIfFaild.h"
#include "D3DUtil.h"
#include "PathManager.h"
#include "imgui.h"

#include <cmath>

namespace
{
	// Root parameter slots, see the header comment in Shaders/Editor/BufferViewer.hlsl.
	enum Root : UINT { RootPass, RootView, RootShadowCB, RootGBuffer, RootShadowMap, RootVelocity, RootCount };

	constexpr UINT FIRST_BUFFER = (UINT)DebugBuffer::Overview + 1;
	constexpr UINT BUFFER_COUNT = (UINT)DebugBuffer::Count - FIRST_BUFFER;
}

BufferVisualizer::BufferVisualizer(GraphicsDevice& device) : _device(&device)
{
	BuildPipeline();
}

template<class F>
void BufferVisualizer::ForEachTile(DebugBuffer buffer, F&& f)
{
	if (buffer == DebugBuffer::Overview)
	{
		for (UINT i = 0; i < BUFFER_COUNT; i++) { f(i, BUFFER_COUNT, (DebugBuffer)(FIRST_BUFFER + i)); }
	}
	else
	{
		f(0, 1, buffer);
	}
}

D3D12_VIEWPORT BufferVisualizer::TileViewport(UINT k, UINT count, Resolution screen)
{
	const UINT cols = (UINT)ceilf(sqrtf((float)count));
	const UINT rows = (count + cols - 1) / cols;
	const float w = screen.Width / (float)cols;
	const float h = screen.Height / (float)rows;
	return { (k % cols) * w, (k / cols) * h, w, h, 0.0f, 1.0f };
}

void BufferVisualizer::DrawLabels(DebugBuffer buffer)
{
	ImDrawList* dl = ImGui::GetForegroundDrawList();
	const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;   // tiles are in back buffer pixels, ImGui in window pixels
	const Resolution bb = _device->BackBufferSize();

	ForEachTile(buffer, [&](UINT k, UINT count, DebugBuffer b)
	{
		D3D12_VIEWPORT vp = TileViewport(k, count, bb);
		ImVec2 pos = { vp.TopLeftX / scale.x + 6.0f, (vp.TopLeftY + vp.Height) / scale.y - 20.0f };
		dl->AddText(pos, IM_COL32(255, 255, 0, 255), ToString(b));
	});
}

void BufferVisualizer::Draw(Renderer& renderer, DebugBuffer buffer)
{
	ID3D12GraphicsCommandList* l = _device->CommandList();
	FrameResource* frame = renderer.GetFrameResources();

	l->SetPipelineState(_pso.Get());
	l->SetGraphicsRootSignature(_rootSignature.Get());

	l->IASetVertexBuffers(0, 0, nullptr);
	l->IASetIndexBuffer(nullptr);
	l->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	l->SetGraphicsRootConstantBufferView(RootPass, frame->PassCB->Resource()->GetGPUVirtualAddress());
	l->SetGraphicsRootConstantBufferView(RootShadowCB, frame->ShadowCB->Resource()->GetGPUVirtualAddress());

	// Albedo is the first of 5 contiguous G-buffer SRVs: Albedo, Normal, MatData, Depth, Stencil.
	l->SetGraphicsRootDescriptorTable(RootGBuffer, renderer.GetGBufferTextures(GBufferIndex::Albedo).SRV.Gpu());
	l->SetGraphicsRootDescriptorTable(RootShadowMap, renderer.GetShadowSrv().Gpu());
	l->SetGraphicsRootDescriptorTable(RootVelocity, renderer.GetVelocitySrv().Gpu());

	const Resolution bb = _device->BackBufferSize();

	ForEachTile(buffer, [&](UINT k, UINT count, DebugBuffer b)
	{
		// The viewport squeezes the fullscreen triangle into the tile, so the whole frame is shown scaled down.
		D3D12_VIEWPORT vp = TileViewport(k, count, bb);
		D3D12_RECT scissor = { (LONG)vp.TopLeftX, (LONG)vp.TopLeftY, (LONG)(vp.TopLeftX + vp.Width), (LONG)(vp.TopLeftY + vp.Height) };
		l->RSSetViewports(1, &vp);
		l->RSSetScissorRects(1, &scissor);

		UINT index = (UINT)b;
		l->SetGraphicsRoot32BitConstants(RootView, 1, &index, 0);

		l->DrawInstanced(3, 1, 0, 0);
	});

	// Restore the full screen for whatever draws next (ImGui sets its own, but don't rely on it).
	D3D12_VIEWPORT full = { 0.0f, 0.0f, (float)bb.Width, (float)bb.Height, 0.0f, 1.0f };
	D3D12_RECT fullRect = { 0, 0, (LONG)bb.Width, (LONG)bb.Height };
	l->RSSetViewports(1, &full);
	l->RSSetScissorRects(1, &fullRect);
}

void BufferVisualizer::BuildPipeline()
{
	{
		CD3DX12_DESCRIPTOR_RANGE gbuffer;  gbuffer.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0);   // t0-t4
		CD3DX12_DESCRIPTOR_RANGE shadow;   shadow.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);    // t5
		CD3DX12_DESCRIPTOR_RANGE velocity; velocity.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);  // t6

		CD3DX12_ROOT_PARAMETER params[RootCount];
		params[RootPass].InitAsConstantBufferView(0);          // b0 cbPass
		params[RootView].InitAsConstants(1, 1);                // b1 buffer index
		params[RootShadowCB].InitAsConstantBufferView(2);      // b2 cbShadow
		params[RootGBuffer].InitAsDescriptorTable(1, &gbuffer);
		params[RootShadowMap].InitAsDescriptorTable(1, &shadow);
		params[RootVelocity].InitAsDescriptorTable(1, &velocity);

		// s6 must match the light pass (Renderer::GetStaticSamplers), otherwise the shadow view differs from real shadows.
		CD3DX12_STATIC_SAMPLER_DESC samplers[2] =
		{
			CD3DX12_STATIC_SAMPLER_DESC(1, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP),
			CD3DX12_STATIC_SAMPLER_DESC(6, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER, D3D12_TEXTURE_ADDRESS_MODE_BORDER,
				0.0f, 16, D3D12_COMPARISON_FUNC_LESS_EQUAL, D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK),
		};

		CD3DX12_ROOT_SIGNATURE_DESC desc(RootCount, params, _countof(samplers), samplers, D3D12_ROOT_SIGNATURE_FLAG_NONE);

		ComPtr<ID3DBlob> serialized, error;
		HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &error);
		if (error) { OutputDebugStringA((char*)error->GetBufferPointer()); }
		ThrowIfFailed(hr);

		ThrowIfFailed(_device->Device()->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&_rootSignature)));
	}

	{
		const std::wstring path = LOCAL_PATH_W + L"Shaders/Editor/BufferViewer.hlsl";
		ComPtr<ID3DBlob> vs = D3DUtil::CompileShader(path, nullptr, "VS", "vs_5_1");
		ComPtr<ID3DBlob> ps = D3DUtil::CompileShader(path, nullptr, "PS", "ps_5_1");

		D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
		pso.pRootSignature = _rootSignature.Get();
		pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
		pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

		pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);   // tiles are opaque

		pso.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		pso.DepthStencilState.DepthEnable = FALSE;

		pso.SampleMask = UINT_MAX;
		pso.SampleDesc.Count = 1;
		pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

		pso.NumRenderTargets = 1;
		pso.RTVFormats[0] = _device->BackBufferFormat();
		pso.DSVFormat = DXGI_FORMAT_UNKNOWN;

		ThrowIfFailed(_device->Device()->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&_pso)));
	}
}
