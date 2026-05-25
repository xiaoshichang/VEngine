#include "Engine/Runtime/Resource/ResourceManager.h"

#include <utility>

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

    UInt64 ResourceManager::GetChangeSerial() const noexcept
    {
        return changeSerial_;
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

    ResourceHandle<MeshResource> ResourceManager::CreateMesh(std::string name, std::vector<MeshVertex> vertices)
    {
        MeshResource mesh;
        mesh.id = AllocateResourceId();
        mesh.revision = NextResourceRevision(mesh.revision);
        mesh.name = std::move(name);
        mesh.vertices = std::move(vertices);

        ResourceHandle<MeshResource> handle(mesh.id);
        meshes_.emplace(mesh.id, std::move(mesh));
        MarkResourcesChanged();
        return handle;
    }

    bool ResourceManager::UpdateMesh(ResourceHandle<MeshResource> handle, std::vector<MeshVertex> vertices)
    {
        auto iter = meshes_.find(handle.GetId());
        if (iter == meshes_.end())
        {
            return false;
        }

        MeshResource& mesh = iter->second;
        mesh.vertices = std::move(vertices);
        mesh.revision = NextResourceRevision(mesh.revision);
        MarkResourcesChanged();
        return true;
    }

    bool ResourceManager::DestroyMesh(ResourceHandle<MeshResource> handle)
    {
        const SizeT erasedCount = meshes_.erase(handle.GetId());
        if (erasedCount == 0)
        {
            return false;
        }

        if (fallbackMesh_ == handle)
        {
            fallbackMesh_ = ResourceHandle<MeshResource>();
        }

        MarkResourcesChanged();
        return true;
    }

    ResourceHandle<MaterialResource> ResourceManager::CreateMaterial(std::string name, const Vector3& baseColor)
    {
        MaterialResource material;
        material.id = AllocateResourceId();
        material.revision = NextResourceRevision(material.revision);
        material.name = std::move(name);
        material.baseColor = baseColor;

        ResourceHandle<MaterialResource> handle(material.id);
        materials_.emplace(material.id, std::move(material));
        MarkResourcesChanged();
        return handle;
    }

    bool ResourceManager::UpdateMaterial(ResourceHandle<MaterialResource> handle, const Vector3& baseColor)
    {
        auto iter = materials_.find(handle.GetId());
        if (iter == materials_.end())
        {
            return false;
        }

        MaterialResource& material = iter->second;
        material.baseColor = baseColor;
        material.revision = NextResourceRevision(material.revision);
        MarkResourcesChanged();
        return true;
    }

    bool ResourceManager::DestroyMaterial(ResourceHandle<MaterialResource> handle)
    {
        const SizeT erasedCount = materials_.erase(handle.GetId());
        if (erasedCount == 0)
        {
            return false;
        }

        if (defaultMaterial_ == handle)
        {
            defaultMaterial_ = ResourceHandle<MaterialResource>();
        }

        MarkResourcesChanged();
        return true;
    }

    void ResourceManager::CreateBuiltInResources()
    {
        MeshResource cube;
        cube.id = AllocateResourceId();
        cube.revision = NextResourceRevision(cube.revision);
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
        material.revision = NextResourceRevision(material.revision);
        material.name = "DefaultMaterial";
        material.baseColor = Vector3::One();
        defaultMaterial_ = ResourceHandle<MaterialResource>(material.id);
        materials_.emplace(material.id, std::move(material));

        MarkResourcesChanged();
    }

    ResourceId ResourceManager::AllocateResourceId() noexcept
    {
        return nextResourceId_++;
    }

    ResourceRevision ResourceManager::NextResourceRevision(ResourceRevision revision) noexcept
    {
        return revision + 1;
    }

    void ResourceManager::MarkResourcesChanged() noexcept
    {
        ++changeSerial_;
    }
} // namespace ve
