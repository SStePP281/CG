#include "World.h"

using namespace DirectX;

World::World(const AssetManager& assets) : _assets(assets) {}

ObjectId World::Spawn(ModelHandle model, const Transform& t)
{
    auto obj = std::make_unique<MeshObject>();
    obj->Model = model;
    obj->T = t;

    if (const Model* m = _assets.GetModel(model); m && m->Mesh && !m->Mesh->Submeshes.empty())
    {
        obj->LocalBounds = m->Mesh->Submeshes[0].Bounds;
        for (const Submesh& s : m->Mesh->Submeshes)
        {
            BoundingBox::CreateMerged(obj->LocalBounds, obj->LocalBounds, s.Bounds);
        }
    }

    UpdateTransform(*obj);
    obj->PrevWorld = obj->World;

    ObjectId id = _objects.Add(std::move(obj));

    _spatialDirty = true;
    _version++;

    return id;
}

void World::Destroy(ObjectId id)
{
    if (_objects.Remove(id))
    {
        _spatialDirty = true;
        _version++;
    }
}

void World::Update(float dt)
{
    _objects.ForEach([&](ObjectId, MeshObject& obj)
        {
            obj.PrevWorld = obj.World;
        });

    if (_spatialDirty) { RebuildSpatial(); }
}

void World::QueryVisible(const BoundingFrustum& frustum, std::vector<ObjectId>& out) const
{
    out.clear();
    if (_octree) { _octree->Query(frustum, out); }
}

void World::UpdateTransform(MeshObject& obj)
{
    XMMATRIX world = obj.T.Matrix();
    XMStoreFloat4x4(&obj.World, world);
    obj.LocalBounds.Transform(obj.WorldBounds, world);
}

void World::RebuildSpatial()
{
    bool first = true;
    _bounds = BoundingBox();

    _objects.ForEach([&](ObjectId, MeshObject& obj)
        {
            if (first) { _bounds = obj.WorldBounds; first = false; }
            else { BoundingBox::CreateMerged(_bounds, _bounds, obj.WorldBounds); }
        });

    _octree = std::make_unique<Octree<ObjectId>>(_bounds);
    _objects.ForEach([&](ObjectId id, MeshObject& obj) { _octree->Insert(id, obj.WorldBounds); });

    _spatialDirty = false;
}