#include "Engine/Runtime/Resource/ResourceSystem.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

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
        manifest_.Clear();
        projectRoot_ = Path();
        manifestPath_ = Path();
        resourceResolveCallback_ = nullptr;
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

    ErrorCode ResourceSystem::ReloadManifest()
    {
        if (!initialized_)
        {
            return ErrorCode::InvalidState;
        }

        if (manifestPath_.IsEmpty())
        {
            return ErrorCode::InvalidState;
        }

        manifest_.Clear();
        return manifest_.LoadFromFile(manifestPath_);
    }

    ErrorCode ResourceSystem::ReloadManifest(Path manifestPath)
    {
        SetManifestPath(std::move(manifestPath));
        return ReloadManifest();
    }

    Result<ResourceRecord> ResourceSystem::FindResource(const Guid& guid) const
    {
        if (guid.IsEmpty())
        {
            return Result<ResourceRecord>::Failure(Error(ErrorCode::InvalidArgument, "Resource GUID is empty."));
        }

        const ResourceRecord* record = manifest_.Find(guid);
        if (record != nullptr)
        {
            return Result<ResourceRecord>::Success(*record);
        }

        if (resourceResolveCallback_ == nullptr)
        {
            return Result<ResourceRecord>::Failure(Error(ErrorCode::NotFound, "Resource not found."));
        }

        Result<ResourceRecord> resolved = resourceResolveCallback_(guid);
        if (!resolved)
        {
            return Result<ResourceRecord>::Failure(resolved.GetError());
        }

        ResourceRecord recordValue = resolved.MoveValue();
        if (recordValue.guid.IsEmpty())
        {
            recordValue.guid = guid;
        }

        ErrorCode addResult = manifest_.AddOrUpdate(recordValue);
        if (addResult != ErrorCode::None)
        {
            return Result<ResourceRecord>::Failure(Error(addResult, "Failed to register resource."));
        }

        const ResourceRecord* registered = manifest_.Find(guid);
        if (registered == nullptr)
        {
            return Result<ResourceRecord>::Failure(Error(ErrorCode::NotFound, "Resource not found after resolve."));
        }

        return Result<ResourceRecord>::Success(*registered);
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
