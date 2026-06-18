#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetRef.h"
#include "Engine/Runtime/Resource/AssetRecord.h"
#include "Engine/Runtime/Resource/ResourceObject.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ve
{
    struct ResourceSystemInitParam
    {
        Path projectRoot;
    };

    struct ResourceCollectUnusedParams
    {
        std::vector<AssetID> rootAssets;
    };

    class ResourceSystem;

    class ResourceLoadContext
    {
    public:
        ResourceSystem& resourceSystem;
        const IAssetRecordProvider& assetProvider;
        std::vector<AssetID> requestStack;
        std::vector<AssetID> acquiredReferences;
    };

    class ResourceSystem : public NonMovable
    {
    public:
        ResourceSystem() = default;
        ~ResourceSystem() = default;

        [[nodiscard]] ErrorCode Initialize(const ResourceSystemInitParam& desc);
        void Shutdown() noexcept;
        void SetProjectRoot(Path projectRoot) noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;

        [[nodiscard]] Result<ResourceObject*> RequestResource(const AssetID& id,
                                                              const IAssetRecordProvider& provider);
        [[nodiscard]] Result<ResourceObject*> RequestResource(const AssetID& id, ResourceLoadContext& context);

        template<typename TResource>
        [[nodiscard]] Result<AssetRef<TResource>> Request(const AssetID& id, const IAssetRecordProvider& provider);

        [[nodiscard]] ErrorCode ReleaseResource(ResourceObject* resource);
        [[nodiscard]] ErrorCode ReleaseResource(const AssetID& id);
        [[nodiscard]] SizeT CollectUnusedResources(const ResourceCollectUnusedParams& params);
        void ClearCache() noexcept;

    private:
        struct LoadedResourceEntry
        {
            std::unique_ptr<ResourceObject> resource;
            SizeT referenceCount = 0;
        };

        [[nodiscard]] Path ResolveRuntimePath(const AssetRecord& record) const;
        [[nodiscard]] Result<std::unique_ptr<ResourceObject>> CreateResourceObject(const AssetRecord& record);
        void MarkReachableResource(const AssetID& id, std::vector<AssetID>& reachableResources) const;

        Path projectRoot_;
        std::unordered_map<AssetID, LoadedResourceEntry> cache_;
        bool initialized_ = false;
    };

    template<typename TResource>
    Result<AssetRef<TResource>> ResourceSystem::Request(const AssetID& id, const IAssetRecordProvider& provider)
    {
        Result<ResourceObject*> resource = RequestResource(id, provider);
        if (!resource)
        {
            return Result<AssetRef<TResource>>::Failure(resource.GetError());
        }

        if (resource.GetValue()->GetType() != ResourceObjectTraits<TResource>::Type)
        {
            (void)ReleaseResource(id);
            return Result<AssetRef<TResource>>::Failure(
                Error(ErrorCode::InvalidArgument, "Requested resource type does not match the asset record."));
        }

        TResource* typedResource = dynamic_cast<TResource*>(resource.GetValue());
        if (typedResource == nullptr)
        {
            (void)ReleaseResource(id);
            return Result<AssetRef<TResource>>::Failure(
                Error(ErrorCode::InvalidState, "Loaded resource object has an unexpected concrete type."));
        }

        AssetRef<TResource> assetRef;
        assetRef.SetResource(typedResource);
        return Result<AssetRef<TResource>>::Success(std::move(assetRef));
    }
} // namespace ve
