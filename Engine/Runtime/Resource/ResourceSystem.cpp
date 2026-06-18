#include "Engine/Runtime/Resource/ResourceSystem.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace ve
{
    ErrorCode ResourceSystem::Initialize(const ResourceSystemInitParam& desc)
    {
        projectRoot_ = desc.projectRoot;
        initialized_ = true;
        return ErrorCode::None;
    }

    void ResourceSystem::Shutdown() noexcept
    {
        ClearCache();
        projectRoot_ = Path();
        initialized_ = false;
    }

    void ResourceSystem::SetProjectRoot(Path projectRoot) noexcept
    {
        projectRoot_ = std::move(projectRoot);
    }

    bool ResourceSystem::IsInitialized() const noexcept
    {
        return initialized_;
    }

    const Path& ResourceSystem::GetProjectRoot() const noexcept
    {
        return projectRoot_;
    }

    Result<ResourceObject*> ResourceSystem::RequestResource(const AssetID& id, const IAssetRecordProvider& provider)
    {
        ResourceLoadContext context{*this, provider, {}, {}};
        Result<ResourceObject*> resource = RequestResource(id, context);
        if (!resource)
        {
            for (auto it = context.acquiredReferences.rbegin(); it != context.acquiredReferences.rend(); ++it)
            {
                (void)ReleaseResource(*it);
            }
        }

        return resource;
    }

    Result<ResourceObject*> ResourceSystem::RequestResource(const AssetID& id, ResourceLoadContext& context)
    {
        if (!initialized_)
        {
            return Result<ResourceObject*>::Failure(
                Error(ErrorCode::InvalidState, "ResourceSystem is not initialized."));
        }

        if (id.IsEmpty())
        {
            return Result<ResourceObject*>::Failure(Error(ErrorCode::InvalidArgument, "AssetID is empty."));
        }

        if (std::find(context.requestStack.begin(), context.requestStack.end(), id) != context.requestStack.end())
        {
            return Result<ResourceObject*>::Failure(
                Error(ErrorCode::InvalidState, "Resource dependency cycle detected."));
        }

        if (const auto cached = cache_.find(id); cached != cache_.end())
        {
            ++cached->second.referenceCount;
            context.acquiredReferences.push_back(id);
            return Result<ResourceObject*>::Success(cached->second.resource.get());
        }

        Result<AssetRecord> record = context.assetProvider.FindAssetRecord(id);
        if (!record)
        {
            return Result<ResourceObject*>::Failure(record.GetError());
        }

        context.requestStack.push_back(id);
        Result<std::unique_ptr<ResourceObject>> resource = CreateResourceObject(record.GetValue());
        if (!resource)
        {
            context.requestStack.pop_back();
            return Result<ResourceObject*>::Failure(resource.GetError());
        }

        const ErrorCode loadResult = resource.GetValue()->Load(context);
        if (loadResult != ErrorCode::None)
        {
            context.requestStack.pop_back();
            return Result<ResourceObject*>::Failure(Error(loadResult, "Resource dependency load failed."));
        }
        context.requestStack.pop_back();

        LoadedResourceEntry entry;
        entry.resource = resource.MoveValue();
        entry.referenceCount = 1;

        ResourceObject* resourcePointer = entry.resource.get();
        cache_.insert_or_assign(id, std::move(entry));
        context.acquiredReferences.push_back(id);
        return Result<ResourceObject*>::Success(resourcePointer);
    }

    ErrorCode ResourceSystem::ReleaseResource(ResourceObject* resource)
    {
        if (resource == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }

        return ReleaseResource(resource->GetAssetID());
    }

    ErrorCode ResourceSystem::ReleaseResource(const AssetID& id)
    {
        if (id.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const auto it = cache_.find(id);
        if (it == cache_.end())
        {
            return ErrorCode::NotFound;
        }

        if (it->second.referenceCount == 0)
        {
            return ErrorCode::InvalidState;
        }

        const std::vector<AssetID> dependencies = it->second.resource->GetDependencies();
        --it->second.referenceCount;
        if (it->second.referenceCount == 0)
        {
            cache_.erase(it);
        }

        for (const AssetID& dependency : dependencies)
        {
            if (!dependency.IsEmpty())
            {
                (void)ReleaseResource(dependency);
            }
        }

        return ErrorCode::None;
    }

    SizeT ResourceSystem::CollectUnusedResources(const ResourceCollectUnusedParams& params)
    {
        std::vector<AssetID> reachableResources;
        for (const AssetID& root : params.rootAssets)
        {
            MarkReachableResource(root, reachableResources);
        }

        SizeT unloadedCount = 0;
        for (auto it = cache_.begin(); it != cache_.end();)
        {
            const bool reachable =
                std::find(reachableResources.begin(), reachableResources.end(), it->first) != reachableResources.end();
            if (reachable || it->second.referenceCount > 0)
            {
                ++it;
                continue;
            }

            it = cache_.erase(it);
            ++unloadedCount;
        }

        return unloadedCount;
    }

    void ResourceSystem::ClearCache() noexcept
    {
        cache_.clear();
    }

    Path ResourceSystem::ResolveRuntimePath(const AssetRecord& record) const
    {
        if (record.runtimePath.IsEmpty())
        {
            return Path();
        }

        return projectRoot_ / record.runtimePath;
    }

    Result<std::unique_ptr<ResourceObject>> ResourceSystem::CreateResourceObject(const AssetRecord& record)
    {
        const Path physicalPath = ResolveRuntimePath(record);
        if (physicalPath.IsEmpty())
        {
            return Result<std::unique_ptr<ResourceObject>>::Failure(
                Error(ErrorCode::InvalidArgument, "Resource runtime path is empty."));
        }

        switch (record.type)
        {
        case ResourceType::Scene:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(
                std::make_unique<SceneResource>(record, text.MoveValue()));
        }
        case ResourceType::Mesh:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(
                std::make_unique<MeshResource>(record, text.MoveValue()));
        }
        case ResourceType::Material:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(
                std::make_unique<MaterialResource>(record, text.MoveValue()));
        }
        case ResourceType::Texture:
        {
            Result<std::vector<std::byte>> bytes = FileSystem::ReadBinaryFile(physicalPath);
            if (!bytes)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(bytes.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(
                std::make_unique<TextureResource>(record, bytes.MoveValue()));
        }
        case ResourceType::Unknown:
            break;
        }

        return Result<std::unique_ptr<ResourceObject>>::Failure(
            Error(ErrorCode::Unsupported, "Unsupported resource type for runtime loading."));
    }

    void ResourceSystem::MarkReachableResource(const AssetID& id, std::vector<AssetID>& reachableResources) const
    {
        if (id.IsEmpty())
        {
            return;
        }

        if (std::find(reachableResources.begin(), reachableResources.end(), id) != reachableResources.end())
        {
            return;
        }

        const auto it = cache_.find(id);
        if (it == cache_.end())
        {
            return;
        }

        reachableResources.push_back(id);
        for (const AssetID& dependency : it->second.resource->GetDependencies())
        {
            MarkReachableResource(dependency, reachableResources);
        }
    }
} // namespace ve
