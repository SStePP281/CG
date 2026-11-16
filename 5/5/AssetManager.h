#ifndef ASSET_MANAGER_HPP
#define ASSET_MANAGER_HPP

#include <memory>
#include <string>
#include <unordered_map>

#include "GraphicsDevice.h"
#include "TextureManager.h"
#include "SlotPool.h"
#include "Model.h"
#include "Material.h"
#include "ModelParse.h"

class AssetManager
{
public:
    static constexpr UINT MAX_MATERIALS = 256;

    explicit AssetManager(GraphicsDevice& device);

    ModelHandle LoadModel(const std::string& path);
    ModelHandle FindModel(const std::string& path) const;
    const Model* GetModel(ModelHandle h) const { return _models.Get(h); }

    Material* GetMaterial(MaterialHandle h) { return _materials.Get(h); }
    const Material* GetMaterial(MaterialHandle h) const { return _materials.Get(h); }
    MaterialHandle DefaultMaterial() const { return _defaultMaterial; }

    TextureManager& Textures() { return *_textures; }
    const TextureManager& Textures() const { return *_textures; }

    template<class F> void ForEachModel(F&& f) { _models.ForEach(std::forward<F>(f)); }
    template<class F> void ForEachMaterial(F&& f) { _materials.ForEach(std::forward<F>(f)); }

private:
    void LoadTextures(const ModelParse::MeshInfo& meshData);
    std::unordered_map<std::string, MaterialHandle> CreateMaterials(const ModelParse::MeshInfo& meshData);
    std::unique_ptr<Model> CreateModel(const ModelParse::MeshInfo& meshData, const std::unordered_map<std::string, MaterialHandle>& matByName);

    MaterialHandle AddMaterial(std::unique_ptr<Material> mat);

private:
    GraphicsDevice& _device;
    std::unique_ptr<TextureManager> _textures;

    SlotPool<Model> _models;
    std::unordered_map<std::string, ModelHandle> _modelByPath;

    SlotPool<Material> _materials;
    MaterialHandle _defaultMaterial;
};

#endif // !ASSET_MANAGER_HPP