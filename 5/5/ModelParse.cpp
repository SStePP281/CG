#define NOMINMAX

#include "ModelParse.h"

#include <assimp/cimport.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <iostream>
#include <functional>
#include <algorithm>
#include <cctype>

using namespace DirectX;

namespace
{
    static std::string ToLowerAscii(const std::string& s)
    {
        std::string out = s;
        std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::tolower(c); });
        return out;
    }

    static std::string ExtractTextureFilename(const aiString& aiPath)
    {
        try
        {
            std::filesystem::path p(aiPath.C_Str());
            std::string name = p.filename().string();
            return ToLowerAscii(name);
        }
        catch (...)
        {
            return ToLowerAscii(std::string(aiPath.C_Str()));
        }
    }

    static std::string TryGetTexture(const aiMaterial* mat, std::initializer_list<aiTextureType> types)
    {
        aiString texPath;
        for (aiTextureType t : types)
        {
            if (mat->GetTexture(t, 0, &texPath) == AI_SUCCESS) { return ExtractTextureFilename(texPath); }
        }
        return "";
    }

    static ModelParse::MaterialInfo ParseMaterial(const aiMaterial* mat, const std::string& defaultName)
    {
        ModelParse::MaterialInfo mi{};
        mi.Name = defaultName;

        aiString aName;
        if (mat->Get(AI_MATKEY_NAME, aName) == AI_SUCCESS && aName.length > 0) { mi.Name = std::string(aName.C_Str()); }

        aiColor3D diffCol(1.0f, 1.0f, 1.0f);
        if (mat->Get(AI_MATKEY_COLOR_DIFFUSE, diffCol) == AI_SUCCESS) { mi.DiffuseColor = XMFLOAT4(diffCol.r, diffCol.g, diffCol.b, 1.0f); }
        {
            float shininess = 0.0f;
            if (mat->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS && shininess > 0.0f)
            {
                float normalized = std::min(shininess / 1000.0f, 1.0f);
                mi.Roughness = 1.0f - sqrtf(normalized);
            }

            float reflectivity = 0.0f;
            if (mat->Get(AI_MATKEY_REFLECTIVITY, reflectivity) == AI_SUCCESS)
            {
                mi.Metallic = std::min(std::max(reflectivity, 0.0f), 1.0f);
            }
        }

        mi.DiffuseTextureName = TryGetTexture(mat, { aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE });

        mi.NormalTextureName = TryGetTexture(mat, { aiTextureType_NORMALS, aiTextureType_NORMAL_CAMERA, aiTextureType_HEIGHT });

        mi.MetallicTextureName = TryGetTexture(mat, { aiTextureType_METALNESS });

        mi.RoughnessTextureName = TryGetTexture(mat, { aiTextureType_DIFFUSE_ROUGHNESS, aiTextureType_SHININESS });

        mi.AOTextureName = TryGetTexture(mat, { aiTextureType_AMBIENT_OCCLUSION, aiTextureType_LIGHTMAP });

        mi.DisplacementTextureName = TryGetTexture(mat, { aiTextureType_DISPLACEMENT });

        if (mi.Name == "Mettal")
        {
            mi.Metallic = 1.0f;
            mi.Roughness = 0.05f;
        }

        return mi;
    }

    static void ParseMeshHelper(const aiScene* scene, aiMesh* mesh, ModelParse::MeshInfo& meshInfo)
    {
        uint32_t vertexOffset = (uint32_t)meshInfo.Vertices.size();
        uint32_t indexOffset = (uint32_t)meshInfo.Indices32.size();

        for (uint32_t v = 0; v < mesh->mNumVertices; v++)
        {
            Vertex vert;

            vert.Pos =
            {
                mesh->mVertices[v].x,
                mesh->mVertices[v].y,
                mesh->mVertices[v].z
            };

            vert.Normal = mesh->HasNormals() ? XMFLOAT3{ mesh->mNormals[v].x, mesh->mNormals[v].y, mesh->mNormals[v].z } : XMFLOAT3{ 0, 1, 0 };

            vert.TexC = mesh->HasTextureCoords(0) ? XMFLOAT2{ mesh->mTextureCoords[0][v].x, 1.0f - mesh->mTextureCoords[0][v].y } : XMFLOAT2{ 0, 0 };

            if (mesh->HasTangentsAndBitangents())
            {
                XMFLOAT3 T{ mesh->mTangents[v].x,   mesh->mTangents[v].y,   mesh->mTangents[v].z };
                XMFLOAT3 B{ mesh->mBitangents[v].x, mesh->mBitangents[v].y, mesh->mBitangents[v].z };
                XMFLOAT3 N = vert.Normal;

                XMVECTOR t = XMLoadFloat3(&T);
                XMVECTOR b = XMLoadFloat3(&B);
                XMVECTOR n = XMLoadFloat3(&N);

                float handedness = (XMVectorGetX(XMVector3Dot(XMVector3Cross(n, t), b)) < 0.0f) ? -1.0f : 1.0f;

                vert.Tangent = { T.x, T.y, T.z, handedness };
            }
            else
            {
                vert.Tangent = { 1, 0, 0, 1 };
            }

            meshInfo.Vertices.push_back(vert);
        }

        BoundingBox submeshBounds;
        if (mesh->mNumVertices > 0)
        {
            const Vertex* firstVertex = &meshInfo.Vertices[vertexOffset];
            BoundingBox::CreateFromPoints(submeshBounds, mesh->mNumVertices,
                &firstVertex->Pos, sizeof(Vertex));
        }

        for (uint32_t f = 0; f < mesh->mNumFaces; f++)
        {
            aiFace& face = mesh->mFaces[f];
            meshInfo.Indices32.push_back(face.mIndices[0]);
            meshInfo.Indices32.push_back(face.mIndices[1]);
            meshInfo.Indices32.push_back(face.mIndices[2]);
        }

        std::string materialName = "default";
        if (mesh->mMaterialIndex < scene->mNumMaterials)
        {
            aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];

            aiString name;
            if (mat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) { materialName = name.C_Str(); }

            if (meshInfo.Materials.count(materialName) == 0) { meshInfo.Materials[materialName] = ParseMaterial(mat, materialName); }
        }

        ModelParse::SubmeshInfo sub;
        sub.Name = mesh->mName.length > 0 ? mesh->mName.C_Str() : "submesh_" + std::to_string(meshInfo.Submeshes.size());
        sub.MaterialName = materialName;
        sub.VertexOffset = vertexOffset;
        sub.IndexOffset = indexOffset;
        sub.IndexCount = mesh->mNumFaces * 3;
        sub.Bounds = submeshBounds;

        meshInfo.Submeshes.push_back(sub);
    }

    static void ParseNodeHelper(aiNode* node, const aiScene* scene, ModelParse::MeshInfo& meshInfo)
    {
        for (unsigned int i = 0; i < node->mNumMeshes; ++i)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            ParseMeshHelper(scene, mesh, meshInfo);
        }
        for (unsigned int i = 0; i < node->mNumChildren; ++i)
        {
            ParseNodeHelper(node->mChildren[i], scene, meshInfo);
        }
    }
}

ModelParse::MeshInfo ModelParse::LoadOBJ(const std::string& path)
{
    MeshInfo meshInfo;

    const aiScene* scene = aiImportFile(path.c_str(), aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_Triangulate);

    meshInfo.MeshName = path;
    ParseNodeHelper(scene->mRootNode, scene, meshInfo);

    return meshInfo;
}