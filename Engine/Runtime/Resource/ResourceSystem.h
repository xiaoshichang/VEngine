#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetRecord.h"
#include "Engine/Runtime/Resource/AssetRef.h"
#include "Engine/Runtime/Resource/ResourceObject.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ve
{
    class RenderSystem;

    struct ResourceSystemInitParam
    {
        Path projectRoot;
    };

    struct ResourceCollectUnusedParams
    {
        std::vector<AssetID> rootAssets;
    };

    enum class ResourceRenderState
    {
        Uninitialized,
        // The render-thread initialization command has been accepted. Current RenderSystem resource uploads are
        // command-queue based, so this state means "submitted" rather than GPU-complete.
        Queued,
    };

    class ResourceSystem;

    class ResourceLoadContext
    {
    public:
        [[nodiscard]] Result<ResourceObject*> RequestDependency(const AssetID& id);

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

        template<typename TResource>
        [[nodiscard]] Result<AssetRef<TResource>> Request(const AssetID& id, const IAssetRecordProvider& provider);

        /// Ensures render-thread resources for this AssetRef and its dependencies.
        ///
        /// ResourceSystem owns dependency ordering: dependencies are initialized first, while release happens when
        /// the owning CPU ResourceObject reference count reaches zero.
        void EnsureRenderResource(const AssetRefBase& assetRef, RenderSystem& renderSystem);
        [[nodiscard]] ResourceObject* FindLoadedResource(const AssetID& id) noexcept;
        [[nodiscard]] const ResourceObject* FindLoadedResource(const AssetID& id) const noexcept;
        [[nodiscard]] SizeT CollectUnusedResources(const ResourceCollectUnusedParams& params);
        void ClearCache() noexcept;

    private:
        friend class AssetRefBase;
        friend class ResourceLoadContext;

        struct LoadedResourceEntry
        {
            std::unique_ptr<ResourceObject> resource;
            SizeT referenceCount = 0;
            ResourceRenderState renderState = ResourceRenderState::Uninitialized;
            RenderSystem* renderSystem = nullptr;
        };

        [[nodiscard]] Result<ResourceObject*> RequestResourceInternal(const AssetID& id, ResourceLoadContext& context);
        void ReleaseResourceInternal(const AssetID& id);
        void EnsureRenderResourceInternal(const AssetID& id, RenderSystem& renderSystem, std::vector<AssetID>& renderStack);
        void ReleaseEntryRenderResource(LoadedResourceEntry& entry) noexcept;
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
        ResourceLoadContext context{*this, provider, {}, {}};
        Result<ResourceObject*> resource = RequestResourceInternal(id, context);
        if (!resource)
        {
            for (auto it = context.acquiredReferences.rbegin(); it != context.acquiredReferences.rend(); ++it)
            {
                ReleaseResourceInternal(*it);
            }

            return Result<AssetRef<TResource>>::Failure(resource.GetError());
        }

        if (resource.GetValue()->GetType() != ResourceObjectTraits<TResource>::Type)
        {
            ReleaseResourceInternal(id);
            return Result<AssetRef<TResource>>::Failure(Error(ErrorCode::InvalidArgument, "Requested resource type does not match the asset record."));
        }

        TResource* typedResource = dynamic_cast<TResource*>(resource.GetValue());
        if (typedResource == nullptr)
        {
            ReleaseResourceInternal(id);
            return Result<AssetRef<TResource>>::Failure(Error(ErrorCode::InvalidState, "Loaded resource object has an unexpected concrete type."));
        }

        AssetRef<TResource> assetRef;
        assetRef.BindResource(*this, typedResource);
        return Result<AssetRef<TResource>>::Success(std::move(assetRef));
    }
} // namespace ve
