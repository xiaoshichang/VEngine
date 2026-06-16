#pragma once

#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetManifest.h"
#include "Engine/Runtime/Resource/ResourceObject.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ve
{
    struct ResourceSystemInitParam
    {
        Path projectRoot;
    };

    class ResourceLoadOperation
    {
    public:
        explicit ResourceLoadOperation(Result<ResourceObject*> result);

        [[nodiscard]] bool IsComplete() const noexcept;
        [[nodiscard]] const Result<ResourceObject*>& GetResult() const noexcept;
        [[nodiscard]] Result<ResourceObject*>& GetResult() noexcept;

    private:
        Result<ResourceObject*> result_;
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

        [[nodiscard]] Result<ResourceObject*> LoadResource(const AssetRecord& record);

        template<typename TResource>
        [[nodiscard]] Result<TResource*> LoadResource(const AssetRecord& record);

        [[nodiscard]] ResourceLoadOperation LoadResourceAsync(const AssetRecord& record);

        [[nodiscard]] ErrorCode ReleaseResource(ResourceObject* resource);
        void ClearCache() noexcept;

    private:
        struct LoadedResourceEntry
        {
            std::unique_ptr<ResourceObject> resource;
            std::vector<Guid> dependencies;
            SizeT referenceCount = 0;
        };

        [[nodiscard]] Path ResolveRuntimePath(const AssetRecord& record) const;
        [[nodiscard]] Result<std::unique_ptr<ResourceObject>> CreateResourceObject(const AssetRecord& record);
        [[nodiscard]] ErrorCode ReleaseResource(const Guid& guid);

        Path projectRoot_;
        std::unordered_map<Guid, LoadedResourceEntry> cache_;
        bool initialized_ = false;
    };

    template<typename TResource>
    Result<TResource*> ResourceSystem::LoadResource(const AssetRecord& record)
    {
        if (record.type != ResourceObjectTraits<TResource>::Type)
        {
            return Result<TResource*>::Failure(
                Error(ErrorCode::InvalidArgument, "Requested resource type does not match the asset record."));
        }

        Result<ResourceObject*> resource = LoadResource(record);
        if (!resource)
        {
            return Result<TResource*>::Failure(resource.GetError());
        }

        TResource* typedResource = dynamic_cast<TResource*>(resource.GetValue());
        if (typedResource == nullptr)
        {
            return Result<TResource*>::Failure(
                Error(ErrorCode::InvalidState, "Loaded resource object has an unexpected concrete type."));
        }

        return Result<TResource*>::Success(typedResource);
    }
} // namespace ve
