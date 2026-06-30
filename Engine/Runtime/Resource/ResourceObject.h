#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetRecord.h"
#include "Engine/Runtime/Resource/MaterialProperty.h"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace ve
{
    class RenderSystem;
    class ResourceLoadContext;
    class RTMaterialResource;
    struct RTMaterialResourceDesc;
    class RTMeshResource;
    class RTShaderResource;

    class ResourceObject : public NonMovable
    {
    public:
        virtual ~ResourceObject();

        [[nodiscard]] const AssetRecord& GetAssetRecord() const noexcept;
        [[nodiscard]] const AssetID& GetAssetID() const noexcept;
        [[nodiscard]] ResourceType GetType() const noexcept;
        [[nodiscard]] const Path& GetRuntimePath() const noexcept;
        [[nodiscard]] const std::vector<AssetID>& GetDependencies() const noexcept;

        virtual Error Load(ResourceLoadContext& context);
        /// ResourceSystem calls this after dependency render resources have been submitted.
        ///
        /// Implementations should only create or update render-thread proxies through RenderSystem commands. The base
        /// implementation is a no-op for resource types that are CPU-only at the current milestone.
        virtual void InitRenderResource(RenderSystem& renderSystem);
        /// ResourceSystem calls this before the CPU ResourceObject is removed from cache.
        ///
        /// Release commands should be safe even when a queued init command has not executed yet; RT proxies should be
        /// kept alive by shared_ptr captures in the command queue.
        virtual void ReleaseRenderResource(RenderSystem& renderSystem) noexcept;

    protected:
        explicit ResourceObject(AssetRecord record);

    private:
        AssetRecord record_;
    };

    class MeshResource final : public ResourceObject
    {
    public:
        MeshResource(AssetRecord record, std::string text);

        [[nodiscard]] const std::string& GetText() const noexcept;
        [[nodiscard]] std::shared_ptr<RTMeshResource> GetRTMeshResource() const noexcept;

        void InitRenderResource(RenderSystem& renderSystem) override;
        void ReleaseRenderResource(RenderSystem& renderSystem) noexcept override;

    private:
        std::string text_;
        std::shared_ptr<RTMeshResource> rtMeshResource_;
    };

    class MaterialResource final : public ResourceObject
    {
    public:
        MaterialResource(AssetRecord record, std::string text);

        [[nodiscard]] const std::string& GetText() const noexcept;
        [[nodiscard]] const ShaderMaterialLayout& GetMaterialLayout() const noexcept;
        [[nodiscard]] std::shared_ptr<RTMaterialResource> GetRTMaterialResource() const noexcept;
        [[nodiscard]] const std::vector<MaterialPropertyValue>& GetPropertyValues() const noexcept;
        [[nodiscard]] bool IsDirty() const noexcept;
        [[nodiscard]] UInt64 GetRevision() const noexcept;

        Error Load(ResourceLoadContext& context) override;
        void InitRenderResource(RenderSystem& renderSystem) override;
        void SyncRenderResource(RenderSystem& renderSystem);
        void ReleaseRenderResource(RenderSystem& renderSystem) noexcept override;
        [[nodiscard]] ErrorCode SetPropertyValue(std::string_view name, MaterialPropertyValue value);

    private:
        [[nodiscard]] RTMaterialResourceDesc BuildRenderDesc() const;
        void MarkDirty() noexcept;
        void ClearDirty() noexcept;

        std::string text_;
        ShaderMaterialLayout materialLayout_;
        std::vector<MaterialPropertyValue> propertyValues_;
        UInt64 revision_ = 1;
        bool dirty_ = true;
        std::shared_ptr<RTMaterialResource> rtMaterialResource_;
        std::shared_ptr<RTShaderResource> rtShaderResource_;
    };

    class ShaderResource final : public ResourceObject
    {
    public:
        ShaderResource(AssetRecord record, std::string text);

        [[nodiscard]] const std::string& GetText() const noexcept;
        [[nodiscard]] const std::string& GetReflectionText() const noexcept;
        [[nodiscard]] const ShaderMaterialLayout& GetMaterialLayout() const noexcept;
        [[nodiscard]] std::shared_ptr<RTShaderResource> GetRTShaderResource() const noexcept;

        Error Load(ResourceLoadContext& context) override;
        void InitRenderResource(RenderSystem& renderSystem) override;
        void ReleaseRenderResource(RenderSystem& renderSystem) noexcept override;

    private:
        std::string text_;
        std::string reflectionText_;
        ShaderMaterialLayout materialLayout_;
        std::shared_ptr<RTShaderResource> rtShaderResource_;
    };

    class SceneResource final : public ResourceObject
    {
    public:
        SceneResource(AssetRecord record, std::string text);

        [[nodiscard]] const std::string& GetText() const noexcept;

    private:
        std::string text_;
    };

    class TextureResource final : public ResourceObject
    {
    public:
        TextureResource(AssetRecord record, std::vector<std::byte> bytes);

        [[nodiscard]] const std::vector<std::byte>& GetBytes() const noexcept;

    private:
        std::vector<std::byte> bytes_;
    };

    template<typename TResource>
    struct ResourceObjectTraits;

    template<>
    struct ResourceObjectTraits<MeshResource>
    {
        static constexpr ResourceType Type = ResourceType::Mesh;
        static constexpr const char* Name = "MeshResource";
    };

    template<>
    struct ResourceObjectTraits<MaterialResource>
    {
        static constexpr ResourceType Type = ResourceType::Material;
        static constexpr const char* Name = "MaterialResource";
    };

    template<>
    struct ResourceObjectTraits<SceneResource>
    {
        static constexpr ResourceType Type = ResourceType::Scene;
        static constexpr const char* Name = "SceneResource";
    };

    template<>
    struct ResourceObjectTraits<TextureResource>
    {
        static constexpr ResourceType Type = ResourceType::Texture;
        static constexpr const char* Name = "TextureResource";
    };

    template<>
    struct ResourceObjectTraits<ShaderResource>
    {
        static constexpr ResourceType Type = ResourceType::Shader;
        static constexpr const char* Name = "ShaderResource";
    };
} // namespace ve
