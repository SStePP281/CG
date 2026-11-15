#ifndef MODEL_HPP
#define MODEL_HPP

#include <string>
#include <vector>
#include <memory>
#include "SlotHandle.h"
#include "Mesh.h"
#include "Material.h"

struct Model;
using ModelHandle = SlotHandle<Model>;

struct Model
{
    struct Part
    {
        uint32_t Submesh = 0;
        MaterialHandle Material;
    };

    std::string Name;

    std::unique_ptr<MeshGeometry> Mesh;
    std::vector<Part> Parts;
    std::vector<MaterialHandle> Materials;
};

#endif // !MODEL_HPP
