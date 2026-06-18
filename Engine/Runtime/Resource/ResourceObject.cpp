#include "Engine/Runtime/Resource/ResourceObject.h"

#include "Engine/Runtime/Resource/ResourceSystem.h"

#include <utility>

namespace ve
{
    ResourceObject::ResourceObject(AssetRecord record)
        : record_(std::move(record))
    {
    }

    ResourceObject::~ResourceObject() = default;

    const AssetRecord& ResourceObject::GetAssetRecord() const noexcept
    {
        return record_;
    }

    const AssetID& ResourceObject::GetAssetID() const noexcept
    {
        return record_.id;
    }

    ResourceType ResourceObject::GetType() const noexcept
    {
        return record_.type;
    }

    const Path& ResourceObject::GetRuntimePath() const noexcept
    {
        return record_.runtimePath;
    }

    const std::vector<AssetID>& ResourceObject::GetDependencies() const noexcept
    {
        return record_.dependencies;
    }

    ErrorCode ResourceObject::Load(ResourceLoadContext& context)
    {
        for (const AssetID& dependency : record_.dependencies)
        {
            Result<ResourceObject*> dependencyResource = context.RequestDependency(dependency);
            if (!dependencyResource)
            {
                return dependencyResource.GetError().GetCode();
            }
        }

        return ErrorCode::None;
    }

    ErrorCode ResourceObject::InitRenderResource(RenderSystem& renderSystem)
    {
        (void)renderSystem;
        return ErrorCode::None;
    }

    void ResourceObject::ReleaseRenderResource(RenderSystem& renderSystem) noexcept
    {
        (void)renderSystem;
    }

    MeshResource::MeshResource(AssetRecord record, std::string text)
        : ResourceObject(std::move(record))
        , text_(std::move(text))
    {
    }

    const std::string& MeshResource::GetText() const noexcept
    {
        return text_;
    }

    MaterialResource::MaterialResource(AssetRecord record, std::string text)
        : ResourceObject(std::move(record))
        , text_(std::move(text))
    {
    }

    const std::string& MaterialResource::GetText() const noexcept
    {
        return text_;
    }

    SceneResource::SceneResource(AssetRecord record, std::string text)
        : ResourceObject(std::move(record))
        , text_(std::move(text))
    {
    }

    const std::string& SceneResource::GetText() const noexcept
    {
        return text_;
    }

    TextureResource::TextureResource(AssetRecord record, std::vector<std::byte> bytes)
        : ResourceObject(std::move(record))
        , bytes_(std::move(bytes))
    {
    }

    const std::vector<std::byte>& TextureResource::GetBytes() const noexcept
    {
        return bytes_;
    }
} // namespace ve
