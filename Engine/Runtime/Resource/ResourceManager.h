#pragma once

#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Resource/ResourceHandle.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ve
{
    using ResourceRevision = UInt64;

    struct MeshVertex
    {
        Vector3 position = Vector3::Zero();
        Vector3 normal = Vector3::UnitY();
        Vector3 color = Vector3::One();
    };

    struct MeshResource
    {
        ResourceId id = InvalidResourceId;
        ResourceRevision revision = 0;
        std::string name;
        std::vector<MeshVertex> vertices;
    };

    struct MaterialResource
    {
        ResourceId id = InvalidResourceId;
        ResourceRevision revision = 0;
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
        [[nodiscard]] UInt64 GetChangeSerial() const noexcept;

        [[nodiscard]] const MeshResource* FindMesh(ResourceHandle<MeshResource> handle) const noexcept;
        [[nodiscard]] const MaterialResource* FindMaterial(ResourceHandle<MaterialResource> handle) const noexcept;

        [[nodiscard]] ResourceHandle<MeshResource> CreateMesh(std::string name, std::vector<MeshVertex> vertices);
        [[nodiscard]] bool UpdateMesh(ResourceHandle<MeshResource> handle, std::vector<MeshVertex> vertices);
        [[nodiscard]] bool DestroyMesh(ResourceHandle<MeshResource> handle);

        [[nodiscard]] ResourceHandle<MaterialResource> CreateMaterial(std::string name, const Vector3& baseColor);
        [[nodiscard]] bool UpdateMaterial(ResourceHandle<MaterialResource> handle, const Vector3& baseColor);
        [[nodiscard]] bool DestroyMaterial(ResourceHandle<MaterialResource> handle);

        template<typename Visitor>
        void ForEachMesh(Visitor&& visitor) const
        {
            for (const auto& entry : meshes_)
            {
                visitor(entry.second);
            }
        }

        template<typename Visitor>
        void ForEachMaterial(Visitor&& visitor) const
        {
            for (const auto& entry : materials_)
            {
                visitor(entry.second);
            }
        }

    private:
        void CreateBuiltInResources();
        [[nodiscard]] ResourceId AllocateResourceId() noexcept;
        [[nodiscard]] ResourceRevision NextResourceRevision(ResourceRevision revision) noexcept;
        void MarkResourcesChanged() noexcept;

        ResourceId nextResourceId_ = 1;
        UInt64 changeSerial_ = 0;
        ResourceHandle<MeshResource> fallbackMesh_;
        ResourceHandle<MaterialResource> defaultMaterial_;
        std::unordered_map<ResourceId, MeshResource> meshes_;
        std::unordered_map<ResourceId, MaterialResource> materials_;
    };
} // namespace ve
