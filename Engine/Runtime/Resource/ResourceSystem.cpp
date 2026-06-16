#include "Engine/Runtime/Resource/ResourceSystem.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <memory>
#include <utility>

namespace ve
{
    ResourceLoadOperation::ResourceLoadOperation(Result<ResourceObject*> result)
        : result_(std::move(result))
    {
    }

    bool ResourceLoadOperation::IsComplete() const noexcept
    {
        return true;
    }

    const Result<ResourceObject*>& ResourceLoadOperation::GetResult() const noexcept
    {
        return result_;
    }

    Result<ResourceObject*>& ResourceLoadOperation::GetResult() noexcept
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

    Result<ResourceObject*> ResourceSystem::LoadResource(const AssetRecord& record)
    {
        if (!initialized_)
        {
            return Result<ResourceObject*>::Failure(
                Error(ErrorCode::InvalidState, "ResourceSystem is not initialized."));
        }

        if (record.guid.IsEmpty())
        {
            return Result<ResourceObject*>::Failure(Error(ErrorCode::InvalidArgument, "Resource GUID is empty."));
        }

        if (const auto cached = cache_.find(record.guid); cached != cache_.end())
        {
            ++cached->second.referenceCount;
            return Result<ResourceObject*>::Success(cached->second.resource.get());
        }

        Result<std::unique_ptr<ResourceObject>> resource = CreateResourceObject(record);
        if (!resource)
        {
            return Result<ResourceObject*>::Failure(resource.GetError());
        }

        LoadedResourceEntry entry;
        entry.resource = resource.MoveValue();
        entry.dependencies = record.dependencies;
        entry.referenceCount = 1;

        ResourceObject* resourcePointer = entry.resource.get();
        cache_.insert_or_assign(record.guid, std::move(entry));
        return Result<ResourceObject*>::Success(resourcePointer);
    }

    ResourceLoadOperation ResourceSystem::LoadResourceAsync(const AssetRecord& record)
    {
        return ResourceLoadOperation(LoadResource(record));
    }

    ErrorCode ResourceSystem::ReleaseResource(ResourceObject* resource)
    {
        if (resource == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }

        return ReleaseResource(resource->GetGuid());
    }

    ErrorCode ResourceSystem::ReleaseResource(const Guid& guid)
    {
        if (guid.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const auto it = cache_.find(guid);
        if (it == cache_.end())
        {
            return ErrorCode::NotFound;
        }

        if (it->second.referenceCount == 0)
        {
            return ErrorCode::InvalidState;
        }

        const std::vector<Guid> dependencies = it->second.dependencies;
        --it->second.referenceCount;
        if (it->second.referenceCount == 0)
        {
            cache_.erase(it);
        }

        for (const Guid& dependency : dependencies)
        {
            if (!dependency.IsEmpty())
            {
                (void)ReleaseResource(dependency);
            }
        }

        return ErrorCode::None;
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
        case ResourceType::Mesh:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(
                std::make_unique<MeshResource>(record.guid, record.runtimePath, text.MoveValue()));
        }
        case ResourceType::Material:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(
                std::make_unique<MaterialResource>(record.guid, record.runtimePath, text.MoveValue()));
        }
        case ResourceType::Scene:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(
                std::make_unique<SceneResource>(record.guid, record.runtimePath, text.MoveValue()));
        }
        case ResourceType::Text:
        {
            Result<std::string> text = FileSystem::ReadTextFile(physicalPath);
            if (!text)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(text.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(
                std::make_unique<TextResource>(record.guid, record.runtimePath, text.MoveValue()));
        }
        case ResourceType::Binary:
        {
            Result<std::vector<std::byte>> bytes = FileSystem::ReadBinaryFile(physicalPath);
            if (!bytes)
            {
                return Result<std::unique_ptr<ResourceObject>>::Failure(bytes.GetError());
            }

            return Result<std::unique_ptr<ResourceObject>>::Success(
                std::make_unique<BinaryResource>(record.guid, record.runtimePath, bytes.MoveValue()));
        }
        case ResourceType::Unknown:
            break;
        }

        return Result<std::unique_ptr<ResourceObject>>::Failure(
            Error(ErrorCode::Unsupported, "Unsupported resource type for runtime loading."));
    }

} // namespace ve
