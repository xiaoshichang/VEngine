#include "Engine/Runtime/Resource/ResourceSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Render/RenderSystem.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace ve
{
    Result<ResourceObject*> ResourceLoadContext::RequestDependency(const AssetID& id)
    {
        return resourceSystem.RequestResourceInternal(id, *this);
    }

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

    void ResourceSystem::EnsureRenderResource(const AssetRefBase& assetRef, RenderSystem& renderSystem)
    {
        VE_ASSERT_MESSAGE(initialized_, "ResourceSystem::EnsureRenderResource requires an initialized ResourceSystem.");
        VE_ASSERT_MESSAGE(renderSystem.IsInitialized(), "ResourceSystem::EnsureRenderResource requires an initialized RenderSystem.");

        const AssetID& id = assetRef.GetAssetID();
        VE_ASSERT_MESSAGE(!id.IsEmpty(), "ResourceSystem::EnsureRenderResource requires a non-empty AssetID.");
        VE_ASSERT_MESSAGE(assetRef.IsLoaded(), "ResourceSystem::EnsureRenderResource requires a loaded AssetRef.");

        std::vector<AssetID> renderStack;
        EnsureRenderResourceInternal(id, renderSystem, renderStack);
    }

    ResourceObject* ResourceSystem::FindLoadedResource(const AssetID& id) noexcept
    {
        const auto it = cache_.find(id);
        return it != cache_.end() ? it->second.resource.get() : nullptr;
    }

    const ResourceObject* ResourceSystem::FindLoadedResource(const AssetID& id) const noexcept
    {
        const auto it = cache_.find(id);
        return it != cache_.end() ? it->second.resource.get() : nullptr;
    }

    Result<ResourceObject*> ResourceSystem::RequestResourceInternal(const AssetID& id, ResourceLoadContext& context)
    {
        if (!initialized_)
        {
            return Result<ResourceObject*>::Failure(Error(ErrorCode::InvalidState, "ResourceSystem is not initialized."));
        }

        if (id.IsEmpty())
        {
            return Result<ResourceObject*>::Failure(Error(ErrorCode::InvalidArgument, "AssetID is empty."));
        }

        if (std::find(context.requestStack.begin(), context.requestStack.end(), id) != context.requestStack.end())
        {
            return Result<ResourceObject*>::Failure(Error(ErrorCode::InvalidState, "Resource dependency cycle detected."));
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

        const Error loadResult = resource.GetValue()->Load(context);
        if (!loadResult.IsOk())
        {
            context.requestStack.pop_back();
            return Result<ResourceObject*>::Failure(
                Error(loadResult.GetCode(),
                      "Resource '" + id.ToString() + "' at '" + record.GetValue().runtimePath.GetString() + "' failed to load: " + loadResult.GetMessage()));
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

    void ResourceSystem::ReleaseResourceInternal(const AssetID& id)
    {
        VE_ASSERT_MESSAGE(!id.IsEmpty(), "ResourceSystem::ReleaseResourceInternal requires a non-empty AssetID.");

        const auto it = cache_.find(id);
        VE_ASSERT_MESSAGE(it != cache_.end(), "ResourceSystem::ReleaseResourceInternal requires a loaded resource.");

        VE_ASSERT_MESSAGE(it->second.referenceCount > 0, "ResourceSystem::ReleaseResourceInternal encountered an invalid reference count.");

        --it->second.referenceCount;
        if (it->second.referenceCount > 0)
        {
            return;
        }

        const std::vector<AssetID> dependencies = it->second.resource->GetDependencies();
        ReleaseEntryRenderResource(it->second);
        cache_.erase(it);

        for (const AssetID& dependency : dependencies)
        {
            if (!dependency.IsEmpty())
            {
                ReleaseResourceInternal(dependency);
            }
        }
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
            const bool reachable = std::find(reachableResources.begin(), reachableResources.end(), it->first) != reachableResources.end();
            if (reachable || it->second.referenceCount > 0)
            {
                ++it;
                continue;
            }

            ReleaseEntryRenderResource(it->second);
            it = cache_.erase(it);
            ++unloadedCount;
        }

        return unloadedCount;
    }

    void ResourceSystem::ClearCache() noexcept
    {
        for (auto& entry : cache_)
        {
            ReleaseEntryRenderResource(entry.second);
        }

        cache_.clear();
    }

    void ResourceSystem::EnsureRenderResourceInternal(const AssetID& id, RenderSystem& renderSystem, std::vector<AssetID>& renderStack)
    {
        VE_ASSERT_MESSAGE(std::find(renderStack.begin(), renderStack.end(), id) == renderStack.end(), "Resource render dependency cycle detected.");

        const auto it = cache_.find(id);
        VE_ASSERT_MESSAGE(it != cache_.end(), "ResourceSystem::EnsureRenderResourceInternal requires dependencies to be CPU-loaded.");

        LoadedResourceEntry& entry = it->second;
        if (entry.renderState == ResourceRenderState::Queued)
        {
            VE_ASSERT_MESSAGE(entry.renderSystem == &renderSystem, "Resource render resource was initialized with a different RenderSystem.");
            return;
        }

        renderStack.push_back(id);
        for (const AssetID& dependency : entry.resource->GetDependencies())
        {
            if (dependency.IsEmpty())
            {
                continue;
            }

            EnsureRenderResourceInternal(dependency, renderSystem, renderStack);
        }

        entry.resource->InitRenderResource(renderSystem);
        renderStack.pop_back();

        entry.renderState = ResourceRenderState::Queued;
        entry.renderSystem = &renderSystem;
    }

    void ResourceSystem::ReleaseEntryRenderResource(LoadedResourceEntry& entry) noexcept
    {
        if (entry.renderState != ResourceRenderState::Queued || entry.renderSystem == nullptr)
        {
            entry.renderState = ResourceRenderState::Uninitialized;
            entry.renderSystem = nullptr;
            return;
        }

        entry.resource->ReleaseRenderResource(*entry.renderSystem);
        entry.renderState = ResourceRenderState::Uninitialized;
        entry.renderSystem = nullptr;
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
            return Result<std::unique_ptr<ResourceObject>>::Failure(Error(ErrorCode::InvalidArgument, "Resource runtime path is empty."));
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

            return Result<std::unique_ptr<ResourceObject>>::Success(std::make_unique<SceneResource>(record, text.MoveValue()));
        }
        case ResourceType::Mesh:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(std::make_unique<MeshResource>(record, text.MoveValue()));
        }
        case ResourceType::Material:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(std::make_unique<MaterialResource>(record, text.MoveValue()));
        }
        case ResourceType::Shader:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(std::make_unique<ShaderResource>(record, text.MoveValue()));
        }
        case ResourceType::Texture:
        {
            Result<std::vector<std::byte>> bytes = FileSystem::ReadBinaryFile(physicalPath);
            if (!bytes)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(bytes.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(std::make_unique<TextureResource>(record, bytes.MoveValue()));
        }
        case ResourceType::Unknown:
            break;
        }

        return Result<std::unique_ptr<ResourceObject>>::Failure(Error(ErrorCode::Unsupported, "Unsupported resource type for runtime loading."));
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
