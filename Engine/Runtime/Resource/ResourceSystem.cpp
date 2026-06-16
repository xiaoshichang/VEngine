#include "Engine/Runtime/Resource/ResourceSystem.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <algorithm>
#include <utility>

namespace ve
{
    ResourceLoadOperation::ResourceLoadOperation(Result<LoadedResourceData> result)
        : result_(std::move(result))
    {
    }

    bool ResourceLoadOperation::IsComplete() const noexcept
    {
        return true;
    }

    const Result<LoadedResourceData>& ResourceLoadOperation::GetResult() const noexcept
    {
        return result_;
    }

    Result<LoadedResourceData>& ResourceLoadOperation::GetResult() noexcept
    {
        return result_;
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

    Result<LoadedResourceData> ResourceSystem::LoadResource(const ResourceRecord& record)
    {
        if (record.guid.IsEmpty())
        {
            return Result<LoadedResourceData>::Failure(Error(ErrorCode::InvalidArgument, "Resource GUID is empty."));
        }

        if (const auto cached = cache_.find(record.guid); cached != cache_.end())
        {
            return Result<LoadedResourceData>::Success(cached->second.data);
        }

        Result<LoadedResourceData> loaded = LoadFromRecord(record);
        if (!loaded)
        {
            return loaded;
        }

        LoadedResourceEntry entry;
        entry.data = loaded.GetValue();
        entry.dependencies = record.dependencies;
        cache_.insert_or_assign(record.guid, std::move(entry));
        return loaded;
    }

    ResourceLoadOperation ResourceSystem::LoadResourceAsync(const ResourceRecord& record)
    {
        return ResourceLoadOperation(LoadResource(record));
    }

    ErrorCode ResourceSystem::UnloadResource(const Guid& guid)
    {
        if (guid.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const auto removed = cache_.erase(guid);
        return removed > 0 ? ErrorCode::None : ErrorCode::NotFound;
    }

    SizeT ResourceSystem::CollectUnusedResources(const ResourceCollectUnusedParams& params)
    {
        std::vector<Guid> reachableResources;
        for (const Guid& rootGuid : params.rootGuids)
        {
            MarkReachableResource(rootGuid, reachableResources);
        }

        SizeT unloadedCount = 0;
        for (auto it = cache_.begin(); it != cache_.end();)
        {
            const bool reachable =
                std::find(reachableResources.begin(), reachableResources.end(), it->first) != reachableResources.end();
            if (reachable || (it->second.pinned && !params.unloadPinnedResources))
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

    Path ResourceSystem::ResolveRuntimePath(const ResourceRecord& record) const
    {
        if (record.runtimePath.IsEmpty())
        {
            return Path();
        }

        return projectRoot_ / record.runtimePath;
    }

    Result<LoadedResourceData> ResourceSystem::LoadFromRecord(const ResourceRecord& record)
    {
        const Path physicalPath = ResolveRuntimePath(record);
        if (physicalPath.IsEmpty())
        {
            return Result<LoadedResourceData>::Failure(
                Error(ErrorCode::InvalidArgument, "Resource runtime path is empty."));
        }

        LoadedResourceData data;
        data.guid = record.guid;
        data.type = record.type;
        data.runtimePath = record.runtimePath;

        switch (record.type)
        {
        case ResourceType::Mesh:
        case ResourceType::Material:
        case ResourceType::Scene:
        case ResourceType::Text:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<LoadedResourceData>::Failure(text.GetError());
            }

            data.text = text.MoveValue();
            return Result<LoadedResourceData>::Success(std::move(data));
        }
        case ResourceType::Binary:
        {
            Result<std::vector<std::byte>> bytes = FileSystem::ReadBinaryFile(physicalPath);
            if (!bytes)
            {
                return Result<LoadedResourceData>::Failure(bytes.GetError());
            }

            data.bytes = bytes.MoveValue();
            return Result<LoadedResourceData>::Success(std::move(data));
        }
        case ResourceType::Unknown:
            break;
        }

        return Result<LoadedResourceData>::Failure(
            Error(ErrorCode::Unsupported, "Unsupported resource type for runtime loading."));
    }

    void ResourceSystem::MarkReachableResource(const Guid& guid, std::vector<Guid>& reachableResources) const
    {
        if (guid.IsEmpty())
        {
            return;
        }

        if (std::find(reachableResources.begin(), reachableResources.end(), guid) != reachableResources.end())
        {
            return;
        }

        const auto it = cache_.find(guid);
        if (it == cache_.end())
        {
            return;
        }

        reachableResources.push_back(guid);
        for (const Guid& dependency : it->second.dependencies)
        {
            MarkReachableResource(dependency, reachableResources);
        }
    }
} // namespace ve
