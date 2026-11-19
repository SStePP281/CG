#ifndef BUFFER_VISUALIZER_HPP
#define BUFFER_VISUALIZER_HPP

#include <d3d12.h>
#include <wrl/client.h>

#include "GraphicsDevice.h"
#include "Renderer.h"
#include "RenderView.h"

// ViewMode::BufferVisualization: draws G-buffer channels on top of the finished frame.
// One buffer fills the screen; DebugBuffer::Overview shows all of them as a grid.
// Editor-only: owned by the Editor, the renderer knows nothing about it.
class BufferVisualizer
{
public:
	explicit BufferVisualizer(GraphicsDevice& device);

	// ImGui labels; call between ImGui::NewFrame and ImGui::Render.
	void DrawLabels(DebugBuffer buffer);

	// GPU tiles; call with the back buffer bound as render target.
	void Draw(Renderer& renderer, DebugBuffer buffer);

private:
	void BuildPipeline();

	// Tile k of `count`: the whole screen for a single buffer, a grid cell for Overview.
	static D3D12_VIEWPORT TileViewport(UINT k, UINT count, Resolution screen);

	template<class F> static void ForEachTile(DebugBuffer buffer, F&& f);

	GraphicsDevice* _device;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _pso;
};

#endif // !BUFFER_VISUALIZER_HPP
