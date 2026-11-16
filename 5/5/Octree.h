#ifndef OCTREE_HPP
#define OCTREE_HPP

#include <memory>
#include <vector>
#include <DirectXCollision.h>

template<class T>
class Octree
{
public:
    static constexpr int MAX_OBJECTS = 16;
    static constexpr int MAX_DEPTH = 8;

    explicit Octree(const DirectX::BoundingBox& region, int depth = 0) : _region(region), _depth(depth) {}

    void Insert(const T& id, const DirectX::BoundingBox& bounds)
    {
        if (_region.Contains(bounds) == DirectX::DISJOINT) { return; }

        if (IsLeaf() && (_items.size() < MAX_OBJECTS || _depth >= MAX_DEPTH))
        {
            _items.push_back({ id, bounds });
            return;
        }

        if (IsLeaf()) { Subdivide(); }

        if (Octree* child = ChildContaining(bounds)) { child->Insert(id, bounds); }
        else { _items.push_back({ id, bounds }); }
    }

    void Query(const DirectX::BoundingFrustum& frustum, std::vector<T>& out) const
    {
        DirectX::ContainmentType c = frustum.Contains(_region);
        if (c == DirectX::DISJOINT) { return; }
        if (c == DirectX::CONTAINS) { GatherAll(out); return; }

        for (const Item& it : _items)
        {
            if (frustum.Contains(it.Bounds) != DirectX::DISJOINT) { out.push_back(it.Id); }
        }

        for (const auto& child : _children)
        {
            if (child) { child->Query(frustum, out); }
        }
    }

    template<class F>
    void ForEachNode(F&& f) const
    {
        f(_region, _depth);
        for (const auto& child : _children)
        {
            if (child) { child->ForEachNode(f); }
        }
    }

    const DirectX::BoundingBox& Region() const { return _region; }

private:
    struct Item
    {
        T Id;
        DirectX::BoundingBox Bounds;
    };

    bool IsLeaf() const { return !_children[0]; }

    void Subdivide()
    {
        DirectX::XMFLOAT3 c = _region.Center;
        DirectX::XMFLOAT3 h = { _region.Extents.x * 0.5f, _region.Extents.y * 0.5f, _region.Extents.z * 0.5f };

        for (int i = 0; i < 8; i++)
        {
            DirectX::XMFLOAT3 nc = { c.x + h.x * ((i & 1) ? 1.0f : -1.0f),
                                     c.y + h.y * ((i & 2) ? 1.0f : -1.0f),
                                     c.z + h.z * ((i & 4) ? 1.0f : -1.0f) };
            _children[i] = std::make_unique<Octree>(DirectX::BoundingBox(nc, h), _depth + 1);
        }

        std::vector<Item> old = std::move(_items);
        _items.clear();
        for (const Item& it : old)
        {
            if (Octree* child = ChildContaining(it.Bounds)) { child->Insert(it.Id, it.Bounds); }
            else { _items.push_back(it); }
        }
    }

    Octree* ChildContaining(const DirectX::BoundingBox& bounds) const
    {
        for (const auto& child : _children)
        {
            if (child && child->_region.Contains(bounds) == DirectX::CONTAINS) { return child.get(); }
        }
        return nullptr;
    }

    void GatherAll(std::vector<T>& out) const
    {
        for (const Item& it : _items) { out.push_back(it.Id); }
        for (const auto& child : _children)
        {
            if (child) { child->GatherAll(out); }
        }
    }

private:
    DirectX::BoundingBox _region;
    int _depth = 0;

    std::vector<Item> _items;
    std::unique_ptr<Octree> _children[8];
};


#endif // !OCTREE_HPP