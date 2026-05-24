#pragma once

#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Resource/ResourceHandle.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ve
{
    struct MeshVertex
    {
        Vector3 position = Vector3::Zero();
        Vector3 normal = Vector3::UnitY();
        Vector3 color = Vector3::One();
    };

    struct MeshResource
    {
        ResourceId id = InvalidResourceId;
        std::string name;
        std::vector<MeshVertex> vertices;
    };

    struct MaterialResource
    {
        ResourceId id = InvalidResourceId;
        std::string name;
        Vector3 baseColor = Vector3(0.8f, 0.8f, 0.8f);
    };

    struct TextureResource
    {
        ResourceId id = InvalidResourceId;
        std::string name;
    };

    struct ShaderResource
    {
        ResourceId id = InvalidResourceId;
        std::string name;
    };

    class ResourceManager
    {
    public:
        ResourceManager();

        [[nodiscard]] ResourceHandle<MeshResource> GetFallbackMesh() const noexcept;
        [[nodiscard]] ResourceHandle<MaterialResource> GetDefaultMaterial() const noexcept;

        [[nodiscard]] const MeshResource* FindMesh(ResourceHandle<MeshResource> handle) const noexcept;
        [[nodiscard]] const MaterialResource* FindMaterial(ResourceHandle<MaterialResource> handle) const noexcept;

    private:
        void CreateBuiltInResources();
        [[nodiscard]] ResourceId AllocateResourceId() noexcept;

        ResourceId nextResourceId_ = 1;
        ResourceHandle<MeshResource> fallbackMesh_;
        ResourceHandle<MaterialResource> defaultMaterial_;
        std::unordered_map<ResourceId, MeshResource> meshes_;
        std::unordered_map<ResourceId, MaterialResource> materials_;
    };
} // namespace ve
