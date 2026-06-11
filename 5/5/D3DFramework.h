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

#include "BaseD3DApp.h"
#include "FrameResource.h"
#include "UploadBuffer.h"
#include "D3DUtil.h"
#include "MathHelper.h"
#include "ThrowIfFaild.h" 
#include "ModelStruct.h"
#include "ModelParse.h"
#include "GBuffer.h"
#include "ShadowMap.h"
#include "PostProcess.h"

using namespace Microsoft::WRL;
using namespace DirectX;

constexpr int NUM_FRAME_RECOURCES = 3;
constexpr int MAX_DEBUG_LAYER_COUNT = 8;

struct RenderItem
{
    MeshGeometry* Geo;
    Material* Mat;
    BoundingBox Bounds;

    std::string SubmeshName;

    std::string UsedPso;

    int NumFramesDirty = NUM_FRAME_RECOURCES;

    UINT IndexCount;
    UINT StartIndexLocation;
    int BaseVertexLocation;

    std::vector<InstanceData> Instances;
    UINT VisibleInstanceCount = 0;
    UINT InstanceOffset = 0;

    UINT ShadowInstanceCount = 0;
    UINT ShadowInstanceOffset = 0;
};

class OctreeNode
{
public:
    struct InstanceRef
    {
        RenderItem* Item;
        uint32_t Index;
    };

public:
    static const int MAX_OBJECTS = 16;
    static const int MAX_DEPTH = 8;
    static const int NUM_CHILDREN = 8;

    BoundingBox Region;
    std::vector<InstanceRef> Objects;
    std::unique_ptr<OctreeNode> Children[NUM_CHILDREN];
    bool IsLeaf = true;

    OctreeNode(BoundingBox region) : Region(region) {}

    void Insert(RenderItem* item, uint32_t instanceIdx)
    {
        BoundingBox worldBounds;
        XMMATRIX world = XMLoadFloat4x4(&item->Instances[instanceIdx].World);
        item->Bounds.Transform(worldBounds, world);

        if (Region.Contains(worldBounds) == DirectX::DISJOINT) { return; }

        if (IsLeaf && (Objects.size() < MAX_OBJECTS))
        {
            Objects.push_back({ item, instanceIdx });
            return;
        }

        if (IsLeaf) { Subdivide(); }

        bool placed = false;
        for (int i = 0; i < NUM_CHILDREN; i++)
        {
            if (Children[i]->Region.Contains(worldBounds) == DirectX::CONTAINS)
            {
                Children[i]->Insert(item, instanceIdx);
                placed = true;
                break;
            }
        }

        if (!placed) Objects.push_back({ item, instanceIdx });
    }

    void Subdivide()
    {
        IsLeaf = false;
        XMFLOAT3 center = Region.Center;
        XMFLOAT3 extents = Region.Extents;
        XMFLOAT3 h = { extents.x * 0.5f, extents.y * 0.5f, extents.z * 0.5f };

        for (int i = 0; i < NUM_CHILDREN; i++)
        {
            XMFLOAT3 nc = center;
            nc.x += h.x * (i & 1 ? 1 : -1);
            nc.y += h.y * (i & 2 ? 1 : -1);
            nc.z += h.z * (i & 4 ? 1 : -1);
            Children[i] = std::make_unique<OctreeNode>(BoundingBox(nc, h));
        }

        auto it = Objects.begin();
        while (it != Objects.end())
        {
            BoundingBox wb;
            XMMATRIX world = XMLoadFloat4x4(&it->Item->Instances[it->Index].World);
            it->Item->Bounds.Transform(wb, world);

            bool moved = false;
            for (int i = 0; i < NUM_CHILDREN; i++)
            {
                if (Children[i]->Region.Contains(wb) == DirectX::CONTAINS)
                {
                    Children[i]->Insert(it->Item, it->Index);
                    it = Objects.erase(it);
                    moved = true;
                    break;
                }
            }
            if (!moved) ++it;
        }
    }

    void Query(const BoundingFrustum& frustum, std::vector<InstanceRef>& results)
    {
        auto containment = frustum.Contains(Region);
        if (containment == DirectX::DISJOINT) { return; }

        if (containment == DirectX::CONTAINS)
        {
            GatherAll(results);
            return;
        }

        for (InstanceRef& ref : Objects)
        {
            BoundingBox wb;
            XMMATRIX world = XMLoadFloat4x4(&ref.Item->Instances[ref.Index].World);
            ref.Item->Bounds.Transform(wb, world);

            if (frustum.Contains(wb) != DirectX::DISJOINT)
                results.push_back(ref);
        }

        if (!IsLeaf)
        {
            for (int i = 0; i < NUM_CHILDREN; i++)
            {
                Children[i]->Query(frustum, results);
            }
        }
    }

    void GatherAll(std::vector<InstanceRef>& results)
    {
        for (InstanceRef& ref : Objects) { results.push_back(ref); }

        if (!IsLeaf)
        {
            for (int i = 0; i < NUM_CHILDREN; i++)
            {
                Children[i]->GatherAll(results);
            }
        }
    }
};

class D3DFramework : public BaseD3DApp
{
public:
    D3DFramework(HINSTANCE hInstance);
    D3DFramework(const D3DFramework& rhs) = delete;
    D3DFramework& operator=(const D3DFramework& rhs) = delete;
    ~D3DFramework();

    virtual bool Initialize()override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer& gt) override;
    virtual void Draw(const GameTimer& gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer& gt);
    void UpdateCamera(const GameTimer& gt);

    void ComputeSceneBounds();
    
    void CreatePPS();

    void AnimateMaterials(const GameTimer& gt);
    void AnimateLight(const GameTimer& gt);
    void UpdateMaterialCBs(const GameTimer& gt);
    void UpdateMainPassCB(const GameTimer& gt);
    void UpdateLightSB(const GameTimer& gt);
    void UpdateShadowCB(const GameTimer& gt);
    void UpdateInstanceData(const GameTimer& gt);

    void BuildRootSignatureGBuffer();
    void BuildRootSignatureLightPass();
    void BuildRootSignatureShadowPass();

    void BuildGBufferPSO();
    void BuildLightPassPSO();
    void BuildShadowPassPSO();

    void BuildDescriptorHeaps();
    void BuildLightSRV();

    void BuildShadersAndInputLayout();

    void CreateLight();
    void LoadModel(std::string path);
    void CreateSceneObjects();
    void BuildRenderItems();
    void BuildFrameResources();

    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
    void DrawRenderItemsShadow(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

private:
    void ParseMesh(const ModelParse::MeshInfo& meshData);
    void LoadTextures(const ModelParse::MeshInfo& meshData);
    void ParseMaterials(const ModelParse::MeshInfo& meshData);

    ComPtr<ID3D12Resource> CreateDefault1x1Texture(DXGI_FORMAT format, const UINT8 rgba[4], ComPtr<ID3D12Resource>& uploadHeap);
    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

private:

    std::vector<std::unique_ptr<FrameResource>> _frameResources;
    FrameResource* _currFrameResource = nullptr;
    int _currFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> _rootSignatureGBuffer = nullptr;
    ComPtr<ID3D12RootSignature> _rootSignatureLightPass = nullptr;
    ComPtr<ID3D12RootSignature> _rootSignatureShadowPass = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> _geometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> _materials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> _textures;
    std::vector<Light> _lights;

    std::unordered_map<std::string, std::unique_ptr<Model>> _models;
    std::vector<std::unique_ptr<SceneObject>> _sceneObjects;

    std::vector<std::unique_ptr<RenderItem>> _allRitems;
    std::vector<RenderItem*> _opaqueRitems;
    std::vector<RenderItem*> _shadowRitems;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> _shaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> _psos;

    std::vector<D3D12_INPUT_ELEMENT_DESC> _inputLayout;

    PassConstants _mainPassCB;

    std::unique_ptr<GBuffer> _gBuffer;
    std::unique_ptr<ShadowMap> _shadowMap;
    std::unique_ptr<PostProcessChain> _ppChain;

    UINT _lastSlot = 0;
    UINT _gBufferSrvStart = 0;
    UINT _lightSbSrvSlot = 0;
    UINT _shadowSrvStart = 0;

    BoundingFrustum _camFrustum;
    std::unique_ptr<OctreeNode> _octreeRoot;

    XMFLOAT3 _eyePos = { 0.0f, 0.0f, 0.0f };
    XMFLOAT4X4 _view = MathHelper::Identity4x4();
    XMFLOAT4X4 _proj = MathHelper::Identity4x4();

    float _yaw = 0.0f;
    float _pitch = 0.0f;

    XMFLOAT3 _forward = { 0.0f, 0.0f, 1.0f };
    XMFLOAT3 _right = { 1.0f, 0.0f, 0.0f };
    XMFLOAT3 _up = { 0.0f, 1.0f, 0.0f };

    float _moveSpeed = 10.0f;
    float _rotateSpeed = 0.15f;

    BoundingBox _sceneBounds;

    POINT _lastMousePos;

private:
    bool _isDebugMode = false;

    std::vector<D3D12_RECT> BuildScissorRects(int K, int width, int height)
    {
        int cols = (int)ceilf(sqrtf((float)K));
        int rows = (int)ceilf((float)K / cols);

        int cellW = width / cols;
        int cellH = height / rows;

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
