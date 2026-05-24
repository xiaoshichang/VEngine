#include "Engine/Runtime/Resource/ResourceManager.h"

namespace ve
{
    namespace
    {
        void AddQuad(std::vector<MeshVertex>& vertices,
                     const Vector3& normal,
                     const Vector3& color,
                     const Vector3& a,
                     const Vector3& b,
                     const Vector3& c,
                     const Vector3& d)
        {
            vertices.push_back(MeshVertex{a, normal, color});
            vertices.push_back(MeshVertex{b, normal, color});
            vertices.push_back(MeshVertex{c, normal, color});
            vertices.push_back(MeshVertex{a, normal, color});
            vertices.push_back(MeshVertex{c, normal, color});
            vertices.push_back(MeshVertex{d, normal, color});
        }
    } // namespace

    ResourceManager::ResourceManager()
    {
        CreateBuiltInResources();
    }

    ResourceHandle<MeshResource> ResourceManager::GetFallbackMesh() const noexcept
    {
        return fallbackMesh_;
    }

    ResourceHandle<MaterialResource> ResourceManager::GetDefaultMaterial() const noexcept
    {
        return defaultMaterial_;
    }

    const MeshResource* ResourceManager::FindMesh(ResourceHandle<MeshResource> handle) const noexcept
    {
        const auto iter = meshes_.find(handle.GetId());
        return iter == meshes_.end() ? nullptr : &iter->second;
    }

    const MaterialResource* ResourceManager::FindMaterial(ResourceHandle<MaterialResource> handle) const noexcept
    {
        const auto iter = materials_.find(handle.GetId());
        return iter == materials_.end() ? nullptr : &iter->second;
    }

    void ResourceManager::CreateBuiltInResources()
    {
        MeshResource cube;
        cube.id = AllocateResourceId();
        cube.name = "BuiltInCube";

        constexpr Float32 half = 0.5f;
        AddQuad(cube.vertices,
                Vector3(0.0f, 0.0f, 1.0f),
                Vector3(0.0f, 1.0f, 0.0f),
                Vector3(-half, -half, half),
                Vector3(half, -half, half),
                Vector3(half, half, half),
                Vector3(-half, half, half));
        AddQuad(cube.vertices,
                Vector3(0.0f, 0.0f, -1.0f),
                Vector3(1.0f, 0.0f, 0.0f),
                Vector3(half, -half, -half),
                Vector3(-half, -half, -half),
                Vector3(-half, half, -half),
                Vector3(half, half, -half));
        AddQuad(cube.vertices,
                Vector3(1.0f, 0.0f, 0.0f),
                Vector3(0.0f, 0.0f, 1.0f),
                Vector3(half, -half, half),
                Vector3(half, -half, -half),
                Vector3(half, half, -half),
                Vector3(half, half, half));
        AddQuad(cube.vertices,
                Vector3(-1.0f, 0.0f, 0.0f),
                Vector3(1.0f, 1.0f, 0.0f),
                Vector3(-half, -half, -half),
                Vector3(-half, -half, half),
                Vector3(-half, half, half),
                Vector3(-half, half, -half));
        AddQuad(cube.vertices,
                Vector3(0.0f, 1.0f, 0.0f),
                Vector3(0.0f, 1.0f, 1.0f),
                Vector3(-half, half, half),
                Vector3(half, half, half),
                Vector3(half, half, -half),
                Vector3(-half, half, -half));
        AddQuad(cube.vertices,
                Vector3(0.0f, -1.0f, 0.0f),
                Vector3(1.0f, 0.0f, 1.0f),
                Vector3(-half, -half, -half),
                Vector3(half, -half, -half),
                Vector3(half, -half, half),
                Vector3(-half, -half, half));

        fallbackMesh_ = ResourceHandle<MeshResource>(cube.id);
        meshes_.emplace(cube.id, std::move(cube));

        MaterialResource material;
        material.id = AllocateResourceId();
        material.name = "DefaultMaterial";
        material.baseColor = Vector3::One();
        defaultMaterial_ = ResourceHandle<MaterialResource>(material.id);
        materials_.emplace(material.id, std::move(material));
    }

    ResourceId ResourceManager::AllocateResourceId() noexcept
    {
        return nextResourceId_++;
    }
} // namespace ve
