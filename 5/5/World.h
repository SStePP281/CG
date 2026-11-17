#ifndef WORLD_HPP
#define WORLD_HPP

#include <memory>
#include <vector>

#include "Transform.h"
#include "MeshObject.h"
#include "LightObject.h"
#include "GameTimer.h"
#include "Camera.h"
#include "Octree.h"
#include "SlotPool.h"
#include "AssetManager.h"

class World
{
public:
    explicit World(const AssetManager& assets);

    ObjectId Spawn(ModelHandle model, const Transform& t = Transform());
    void Destroy(ObjectId id);

    MeshObject* Get(ObjectId id) { return _objects.Get(id); }
    const MeshObject* Get(ObjectId id) const { return _objects.Get(id); }
    size_t ObjectCount() const { return _objects.Count(); }

    void SetTerrain(TerrainHandle h) { _terrain = h; _version++; }
    TerrainHandle Terrain() const { return _terrain; }

    template<class F> void ForEachObject(F&& f) { _objects.ForEach(std::forward<F>(f)); }

    std::vector<LightObject>& Lights() { return _lights; }
    const std::vector<LightObject>& Lights() const { return _lights; }

    Camera& MainCamera() { return _camera; }
    const Camera& MainCamera() const { return _camera; }

    void Update(GameTimer& gt);

    void QueryVisible(const DirectX::BoundingFrustum& frustum, std::vector<ObjectId>& out) const;
    const DirectX::BoundingBox& Bounds() const { return _bounds; }
    uint64_t Version() const { return _version; }

private:
    void UpdateTransform(MeshObject& obj);
    void RebuildSpatial();

    const AssetManager& _assets;

    SlotPool<MeshObject> _objects;
    std::vector<LightObject> _lights;
    Camera _camera;

    TerrainHandle _terrain;

    std::unique_ptr<Octree<ObjectId>> _octree;
    DirectX::BoundingBox _bounds;
    bool _spatialDirty = true;
    uint64_t _version = 0;
};

#endif // !WORLD_HPP