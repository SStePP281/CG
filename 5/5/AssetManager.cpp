#include "AssetManager.h"
#include "D3DUtil.h"
#include "PathManager.h"
#include <cassert>

AssetManager::AssetManager(GraphicsDevice& device) : _device(device)
{
    _device.BeginImmediate();

    _textures = std::make_unique<TextureManager>(device);
    _defaultMaterial = AddMaterial(std::make_unique<Material>());

    _device.EndImmediate();
}

ModelHandle AssetManager::LoadModel(const std::string& path)
{
    ModelHandle existing = FindModel(path);
    if (existing.Valid()) { return existing; }

    ModelParse parser;
    ModelParse::MeshInfo meshData = parser.LoadOBJ(path);

    LoadTextures(meshData);
    auto matByName = CreateMaterials(meshData);

    std::unique_ptr<Model> model = CreateModel(meshData, matByName);
    model->Name = path;

    ModelHandle h = _models.Add(std::move(model));
    _modelByPath[path] = h;

    return h;
}

ModelHandle AssetManager::FindModel(const std::string& path) const
{
    auto it = _modelByPath.find(path);
    return it != _modelByPath.end() ? it->second : ModelHandle{};
}

TerrainHandle AssetManager::LoadTerrain(const TerrainDesc& desc)
{
    return _terrains.Add(std::make_unique<TerrainData>(_device, desc));
}

AtmosphereHandle AssetManager::LoadAtmosphere(const AtmosphereDesc& desc)
{
    return _atmosphere.Add(std::make_unique<AtmosphereData>(_device, desc));
}

void AssetManager::LoadTextures(const ModelParse::MeshInfo& meshData)
{
    for (const auto& [name, mi] : meshData.Materials)
    {
        const std::string* names[] =
        {
            &mi.DiffuseTextureName, &mi.NormalTextureName, &mi.MetallicTextureName,
            &mi.RoughnessTextureName, &mi.AOTextureName, &mi.DisplacementTextureName,
        };

        for (const std::string* texName : names)
        {
            if (texName->empty()) { continue; }
            _textures->Load(*texName, LOCAL_PATH_W + L"Textures/" + std::wstring(texName->begin(), texName->end()));
        }
    }
}

std::unordered_map<std::string, MaterialHandle> AssetManager::CreateMaterials(const ModelParse::MeshInfo& meshData)
{
    std::unordered_map<std::string, MaterialHandle> result;

    for (const auto& [name, mi] : meshData.Materials)
    {
        auto mat = std::make_unique<Material>();
        mat->Name = name;

        mat->AlbedoMap = _textures->Find(mi.DiffuseTextureName);
        mat->NormalMap = _textures->Find(mi.NormalTextureName);
        mat->MetallicMap = _textures->Find(mi.MetallicTextureName);
        mat->RoughnessMap = _textures->Find(mi.RoughnessTextureName);
        mat->AOMap = _textures->Find(mi.AOTextureName);
        mat->DisplacementMap = _textures->Find(mi.DisplacementTextureName);

        mat->DiffuseAlbedo = mi.DiffuseColor;
        mat->Roughness = mi.Roughness;
        mat->Metallic = mi.Metallic;

        result[name] = AddMaterial(std::move(mat));
    }

    return result;
}

std::unique_ptr<Model> AssetManager::CreateModel(const ModelParse::MeshInfo& meshData, const std::unordered_map<std::string, MaterialHandle>& matByName)
{
    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = meshData.MeshName;

    UINT vbByteSize = (UINT)meshData.Vertices.size() * sizeof(Vertex);
    UINT ibByteSize = (UINT)meshData.Indices32.size() * sizeof(uint32_t);

    ComPtr<ID3D12Resource> vbUpload;
    ComPtr<ID3D12Resource> ibUpload;

    geo->VertexBufferGPU = D3DUtil::CreateDefaultBuffer(_device.Device(), _device.CommandList(), meshData.Vertices.data(), vbByteSize, vbUpload);
    geo->IndexBufferGPU = D3DUtil::CreateDefaultBuffer(_device.Device(), _device.CommandList(), meshData.Indices32.data(), ibByteSize, ibUpload);

    _device.Release().Push(vbUpload);
    _device.Release().Push(ibUpload);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexBufferByteSize = ibByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;

    geo->Submeshes.reserve(meshData.Submeshes.size());
    for (const auto& sub : meshData.Submeshes)
    {
        Submesh s;
        s.Name = sub.Name;
        s.IndexCount = sub.IndexCount;
        s.StartIndexLocation = sub.IndexOffset;
        s.BaseVertexLocation = (INT)sub.VertexOffset;
        s.Bounds = sub.Bounds;

        geo->Submeshes.push_back(s);
    }

    auto model = std::make_unique<Model>();
    model->Name = meshData.MeshName;
    model->Mesh = std::move(geo);

    model->Parts.reserve(meshData.Submeshes.size());
    for (uint32_t i = 0; i < (uint32_t)meshData.Submeshes.size(); i++)
    {
        auto it = matByName.find(meshData.Submeshes[i].MaterialName);
        MaterialHandle mat = (it != matByName.end()) ? it->second : _defaultMaterial;

        model->Parts.push_back({ i, mat });
    }

    model->Materials.reserve(matByName.size());
    for (const auto& [name, h] : matByName) { model->Materials.push_back(h); }

    return model;
}

MaterialHandle AssetManager::AddMaterial(std::unique_ptr<Material> mat)
{
    MaterialHandle h = _materials.Add(std::move(mat));
    assert(h.Index < MAX_MATERIALS);
    return h;
}