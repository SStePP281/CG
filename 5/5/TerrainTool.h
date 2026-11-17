#ifndef TERRAIN_PAINT_TOOL_HPP
#define TERRAIN_PAINT_TOOL_HPP

#include "IEditorTool.h"
#include "TerrainToolPanel.h"
#include "TerrainData.h"
#include "GraphicsDevice.h"
#include "Input.h"
#include "GameTimer.h"
#include "RenderGUI.h"

using TerrainMode = TerrainToolPanel::TerrainMode;

struct TerrainHit
{
	XMFLOAT3 PosW;
	UINT Valid;

	XMFLOAT2 MapUV;
	XMFLOAT2 Pad;
};

class TerrainTool : public IEditorTool
{
public:
	TerrainTool(TerrainData& terrain, GraphicsDevice& device, Input& input, RenderGUI& gui);

	const char* Name() const override { return "Terrain"; }
	IPanel* Panel() override { return _panel; }

	void Update(const GameTimer& gt) override;
	void PreDraw(const GameTimer& gt, const EditorFrameContext& ctx) override;
	void DrawOverlay(const EditorFrameContext& ctx) override;

private:
	void DoPick(D3D12_GPU_VIRTUAL_ADDRESS passCB, float mouseNdcX, float mouseNdcY);
	void DoRing(D3D12_GPU_VIRTUAL_ADDRESS passCB, DescriptorRange& depth);
	void DoBrush();

	void TransitHitToWrite();
	void TransitHitToRead();

private:
	TerrainData* _terrain;
	GraphicsDevice* _device;
	Input* _input;

private:
	BrushSettings _brush;
	TerrainToolPanel* _panel = nullptr;

private:
	void CreateHitBuffer();
	void CreateSrvAndUAV();

	void BuildPickPipeline();
	void BuildRingPipeline();
	void BuildBrushPipeline();

	ComPtr<ID3D12RootSignature> _rootSignatureTerrainPick = nullptr;
	ComPtr<ID3D12PipelineState> _terrainPickPSO = nullptr;

	ComPtr<ID3D12RootSignature> _rootSignatureTerrainRing = nullptr;
	ComPtr<ID3D12PipelineState> _terrainRingPSO = nullptr;

	ComPtr<ID3D12RootSignature> _rootSignatureTerrainBrush = nullptr;
	ComPtr<ID3D12PipelineState> _terrainBrushPSO = nullptr;

	ComPtr<ID3D12Resource> _hitBuffer;

	DescriptorRange _descRange;
	DescriptorRange _brushUavSubRange;
	DescriptorRange _hitSrvSubRange;

	TerrainMode _mode = TerrainMode::Settings;
	float _deltaTime = 0.0f;
};

#endif // !TERRAIN_PAINT_TOOL_HPP
