#ifndef D3D_FRAMEWORK_HPP
#define D3D_FRAMEWORK_HPP

#include <memory>
#include <vector>
#include <string>
#include <array>
#include <unordered_map>

#include <Windows.h>
#include <Windowsx.h>

#include <wrl/client.h>

#include <d3d12.h>
#include <d3dx12.h>
#include <dxgiformat.h>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <DDSTextureLoader.h>

#include "FrameResource.h"
#include "UploadBuffer.h"
#include "D3DUtil.h"
#include "MathHelper.h"
#include "ThrowIfFaild.h" 
#include "Mesh.h"
#include "Model.h"
#include "Material.h"
#include "Texture.h"
#include "LightObject.h"
#include "ShaderTypes.h"
#include "ModelParse.h"
#include "GBuffer.h"
#include "ShadowMap.h"
#include "PostProcess.h"
#include "Terrain.h"
#include "VelocityBuffers.h"
#include "AntiAliasing.h"
#include "NGXManager.h"
#include "RenderGUI.h"
#include "TextureManager.h"
#include "DeferredReleaseQueue.h"
#include "FrameStats.h"
#include "InfoPanel.h"
#include "SlotPool.h"
#include "Material.h"
#include "GameTimer.h"
#include "Window.h"
#include "Input.h"
#include "AssetManager.h"
#include "World.h"
#include "Octree.h"

class TAA; 
class DLAA;
class NoAA;

using Microsoft::WRL::ComPtr;
using namespace DirectX;

constexpr int MAX_DEBUG_LAYER_COUNT = 9;

struct RenderItem
{
    MeshGeometry* Geo;
    MaterialHandle Material;

    uint32_t SubmeshIndex;

    std::vector<const MeshObject*> Scratch;

    std::string UsedPso;

    UINT IndexCount;
    UINT StartIndexLocation;
    int BaseVertexLocation;

    UINT VisibleInstanceCount = 0;
    UINT InstanceOffset = 0;

    UINT ShadowInstanceCount = 0;
    UINT ShadowInstanceOffset = 0;
};

class D3DFramework
{
public:
    D3DFramework(Window& window, Input& input, AssetManager& assets, World& world, GraphicsDevice& device, RenderSettings& renderSettings);
    ~D3DFramework();

    bool Initialize();
    void Update(const GameTimer& gt);
    void Draw(const GameTimer& gt);
    void OnDisplayResize(Resolution display);

public:
    const FrameStats& GetFrameStats() const { return _frameStats; }
    const FrameTimeHistory& GetFrameHistory() const { return _frameHistory; }

    Terrain& GetTerrain() { return *_terrain; }
    D3D12_GPU_VIRTUAL_ADDRESS PassCBAddress() const { return _currFrameResource->PassCB->Resource()->GetGPUVirtualAddress(); }
    DescriptorRange DepthSrv() const { return _gBuffer->Textures[(UINT)GBufferIndex::Depth].SRV; }

private:
    float AspectRatio() const { return (float)_display.Width / _display.Height; }
    bool ShadowsActive() const;

    void UpdateRenderResolution();
    void OnRenderResize();

    void OnKeyboardInput(const GameTimer& gt);
    
    void CreatePPS();

    void AnimateMaterials(const GameTimer& gt);
    void AnimateLight(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateLightSB(const GameTimer& gt);
    void UpdateShadowCB(const GameTimer& gt);
    void UpdateTerrainCB(const GameTimer& gt);
    void UpdateInstanceData(const GameTimer& gt);

    void BuildRootSignatureGBuffer();
    void BuildRootSignatureLightPass();
    void BuildRootSignatureShadowPass();
    void BuildRootSignatureTerrainPass();

    void BuildGBufferPSO();
    void BuildLightPassPSO();
    void BuildShadowPassPSO();
    void BuildTerrainPassPSO();

    void BuildShadersAndInputLayout();

    void BuildRenderItems();
    void BuildFrameResources();

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
    void DrawRenderItemsShadow(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
    void DrawTerrain(ID3D12GraphicsCommandList* cmdList);
    void DrawTerrainShadow(ID3D12GraphicsCommandList* cmdList, const D3D12_GPU_VIRTUAL_ADDRESS& cascadeCB);

private:
    void LoadIBLTextures();

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

    AntiAliasing* GetAAByMode(AASettings::AAMode mode);

private:
    Window* _window;
    Input& _input;
    AssetManager* _assets;
    World* _world;
    GraphicsDevice* _device;

    FrameStats _frameStats;
    FrameTimeHistory _frameHistory;

    RenderSettings& _renderSettings;
    Resolution _render;
    Resolution _display;

    D3D12_VIEWPORT _screenViewport;
    D3D12_RECT _scissorRect;

    D3D12_VIEWPORT _renderViewport;
    D3D12_RECT _renderScissor;

    std::unordered_map<ModelHandle, std::vector<RenderItem*>> _modelBatches;
    uint64_t _builtWorldVersion = UINT64_MAX;
    std::vector<ObjectId> _visibleObjects;
    BoundingFrustum _camFrustum;

private:
    std::vector<std::unique_ptr<FrameResource>> _frameResources;
    FrameResource* _currFrameResource = nullptr;

    ComPtr<ID3D12RootSignature> _rootSignatureGBuffer = nullptr;
    ComPtr<ID3D12RootSignature> _rootSignatureLightPass = nullptr;
    ComPtr<ID3D12RootSignature> _rootSignatureShadowPass = nullptr;
    ComPtr<ID3D12RootSignature> _rootSignatureTerrainPass = nullptr;

    std::vector<std::unique_ptr<RenderItem>> _allRitems;
    std::vector<RenderItem*> _opaqueRitems;
    std::vector<RenderItem*> _shadowRitems;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> _shaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> _psos;

    std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;

    PassConstants _mainPassCB;

    std::unique_ptr<GBuffer> _gBuffer;
    std::unique_ptr<ShadowMap> _shadowMap;
    std::unique_ptr<Terrain> _terrain;
    std::unique_ptr<VelocityBuffers> _velocityBuffer;
    std::unique_ptr<PostProcessChain> _ppChain;
    std::unique_ptr<NGXManager> _ngxManager;

    UINT _terrainShadowCBOffset = 0;

    AntiAliasing* _currentAA = nullptr;
    std::unique_ptr<NoAA> _noAA;
    std::unique_ptr<TAA> _TAA;
    std::unique_ptr<DLAA> _DLAA;

    ComPtr<ID3D12Resource> _iblIrradiance;
    ComPtr<ID3D12Resource> _iblSpecular;
    ComPtr<ID3D12Resource> _iblBrdf;
    DescriptorRange _iblSrv;

private:
    bool _isDebugMode = false;
    bool _isTerrainDegub = false;
    bool _isUseBeckmann = false;

    bool _renderSettingsDirty = false;

    std::vector<D3D12_RECT> BuildScissorRects(int K, Resolution res)
    {
        int cols = (int)ceilf(sqrtf((float)K));
        int rows = (int)ceilf((float)K / cols);

        int cellW = res.Width / cols;
        int cellH = res.Height / rows;

        std::vector<D3D12_RECT> rects;
        rects.reserve(K);

        for (int i = 0; i < K; i++)
        {
            int col = i % cols;
            int row = i / cols;

            D3D12_RECT r;
            r.left = col * cellW;
            r.top = row * cellH;
            r.right = r.left + cellW;
            r.bottom = r.top + cellH;

            rects.push_back(r);
        }

        return rects;
    }
};

#endif // !D3D_FRAMEWORK_HPP
