#include "Engine/Runtime/Resource/ResourceSystem.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <utility>

namespace ve
{
    ErrorCode ResourceSystem::Initialize(const ResourceSystemInitParam& desc)
    {
        projectRoot_ = desc.projectRoot;
        environment_ = desc.environment;
        initialized_ = true;
        return ErrorCode::None;
    }

    void ResourceSystem::Shutdown() noexcept
    {
        ClearCache();
        manifest_.Clear();
        projectRoot_ = Path();
        manifestPath_ = Path();
        resourceResolveCallback_ = nullptr;
        environment_ = ResourceSystemEnvironment::Player;
        initialized_ = false;
    }

    void ResourceSystem::SetProjectRoot(Path projectRoot) noexcept
    {
        projectRoot_ = std::move(projectRoot);
    }

    void ResourceSystem::SetManifestPath(Path manifestPath) noexcept
    {
        manifestPath_ = std::move(manifestPath);
    }

    bool ResourceSystem::IsInitialized() const noexcept
    {
        return initialized_;
    }

    ResourceSystemEnvironment ResourceSystem::GetEnvironment() const noexcept
    {
        return environment_;
    }

    const Path& ResourceSystem::GetProjectRoot() const noexcept
    {
        return projectRoot_;
    }

    const Path& ResourceSystem::GetManifestPath() const noexcept
    {
        return manifestPath_;
    }

    const ResourceManifest& ResourceSystem::GetManifest() const noexcept
    {
        return manifest_;
    }

    ResourceManifest& ResourceSystem::GetManifest() noexcept
    {
        return manifest_;
    }

    void ResourceSystem::SetResourceResolveCallback(ResourceResolveCallback callback) noexcept
    {
        resourceResolveCallback_ = std::move(callback);
    }

    Result<ResourceRecord> ResourceSystem::FindResource(const Guid& guid) const
    {
        if (guid.IsEmpty())
        {
            return Result<ResourceRecord>::Failure(Error(ErrorCode::InvalidArgument, "Resource GUID is empty."));
        }

        if (environment_ == ResourceSystemEnvironment::Player)
        {
            const ResourceRecord* record = manifest_.Find(guid);
            if (record != nullptr)
            {
                return Result<ResourceRecord>::Success(*record);
            }
            return Result<ResourceRecord>::Failure(Error(ErrorCode::NotFound, "Resource not found."));
        }
        else if(environment_ == ResourceSystemEnvironment::Editor)
        {
            if (resourceResolveCallback_ == nullptr)
            {
                return Result<ResourceRecord>::Failure(
                    Error(ErrorCode::InvalidState, "Editor resource lookup requires a resolve callback."));
            }

            Result<ResourceRecord> resolved = resourceResolveCallback_(guid);
            if (!resolved)
            {
                return Result<ResourceRecord>::Failure(resolved.GetError());
            }

            ResourceRecord record = resolved.MoveValue();
            return Result<ResourceRecord>::Success(std::move(record));
        }
        else
        {
            return Result<ResourceRecord>::Failure(Error(ErrorCode::InvalidState, "Unknown resource system environment."));
        }
    }

    Result<LoadedResourceData> ResourceSystem::LoadResource(const Guid& guid)
    {
        if (guid.IsEmpty())
        {
            return Result<LoadedResourceData>::Failure(Error(ErrorCode::InvalidArgument, "Resource GUID is empty."));
        }

        if (const auto cached = cache_.find(guid); cached != cache_.end())
        {
            return Result<LoadedResourceData>::Success(cached->second);
        }

        Result<ResourceRecord> recordResult = FindResource(guid);
        if (!recordResult)
        {
            return Result<LoadedResourceData>::Failure(recordResult.GetError());
        }

        Result<LoadedResourceData> loaded = LoadFromRecord(recordResult.GetValue());
        if (!loaded)
        {
            return loaded;
        }

        cache_.insert_or_assign(guid, loaded.GetValue());
        return loaded;
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
} // namespace ve
