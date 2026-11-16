#ifndef MESH_OBJECT_HPP
#define MESH_OBJECT_HPP

#include <DirectXMath.h>
#include "Transform.h"
#include "SlotHandle.h"
#include "Model.h"

struct MeshObject
{
    ModelHandle Model;
    Transform T;

    DirectX::XMFLOAT4X4 World;
    DirectX::XMFLOAT4X4 PrevWorld;

    DirectX::BoundingBox LocalBounds;
    DirectX::BoundingBox WorldBounds;
};
using ObjectId = SlotHandle<MeshObject>;

#endif // !MESH_OBJECT_HPP
