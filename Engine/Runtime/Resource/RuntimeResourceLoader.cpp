#include "Engine/Runtime/Resource/RuntimeResourceLoader.h"

#include <algorithm>

namespace ve
{
    namespace
    {
        [[nodiscard]] bool ContainsGuid(const std::vector<Guid>& values, const Guid& guid)
        {
            return std::find(values.begin(), values.end(), guid) != values.end();
        }
    } // namespace

    ErrorCode RuntimeResourceLoader::Initialize(const RuntimeResourceLoaderInitParam& desc)
    {
        if (desc.manifestPath.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        projectRoot_ = desc.projectRoot;
        manifestPath_ = desc.manifestPath;
        manifest_.Clear();

        const ErrorCode loadResult = manifest_.LoadFromFile(manifestPath_);
        if (loadResult != ErrorCode::None)
        {
            projectRoot_ = Path();
            manifestPath_ = Path();
            return loadResult;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void RuntimeResourceLoader::Shutdown() noexcept
    {
        manifest_.Clear();
        projectRoot_ = Path();
        manifestPath_ = Path();
        initialized_ = false;
    }

    bool RuntimeResourceLoader::IsInitialized() const noexcept
    {
        return initialized_;
    }

    const Path& RuntimeResourceLoader::GetProjectRoot() const noexcept
    {
        return projectRoot_;
    }

    const Path& RuntimeResourceLoader::GetManifestPath() const noexcept
    {
        return manifestPath_;
    }

    const ResourceManifest& RuntimeResourceLoader::GetManifest() const noexcept
    {
        return manifest_;
    }

    Result<ResourceRecord> RuntimeResourceLoader::FindResource(const Guid& guid) const
    {
        if (!initialized_)
        {
            return Result<ResourceRecord>::Failure(
                Error(ErrorCode::InvalidState, "RuntimeResourceLoader is not initialized."));
        }

        if (guid.IsEmpty())
        {
            return Result<ResourceRecord>::Failure(Error(ErrorCode::InvalidArgument, "Resource GUID is empty."));
        }

        const ResourceRecord* record = manifest_.Find(guid);
        if (record == nullptr)
        {
            return Result<ResourceRecord>::Failure(
                Error(ErrorCode::NotFound, "Resource not found in runtime manifest."));
        }

        return Result<ResourceRecord>::Success(*record);
    }

    Result<ResourceRecord> RuntimeResourceLoader::FindResource(const Path& runtimePath) const
    {
        if (!initialized_)
        {
            return Result<ResourceRecord>::Failure(
                Error(ErrorCode::InvalidState, "RuntimeResourceLoader is not initialized."));
        }

        if (runtimePath.IsEmpty())
        {
            return Result<ResourceRecord>::Failure(Error(ErrorCode::InvalidArgument, "Resource path is empty."));
        }

        const ResourceRecord* record = manifest_.FindByRuntimePath(runtimePath);
        if (record == nullptr)
        {
            return Result<ResourceRecord>::Failure(
                Error(ErrorCode::NotFound, "Resource path not found in runtime manifest."));
        }

        return Result<ResourceRecord>::Success(*record);
    }

    Result<std::vector<ResourceRecord>> RuntimeResourceLoader::ResolveLoadOrder(const Guid& guid) const
    {
        std::vector<ResourceRecord> loadOrder;
        std::vector<Guid> visiting;
        std::vector<Guid> visited;
        return ResolveLoadOrderRecursive(guid, loadOrder, visiting, visited);
    }

    Result<std::vector<ResourceRecord>> RuntimeResourceLoader::ResolveLoadOrder(const Path& runtimePath) const
    {
        Result<ResourceRecord> record = FindResource(runtimePath);
        if (!record)
        {
            return Result<std::vector<ResourceRecord>>::Failure(record.GetError());
        }

        return ResolveLoadOrder(record.GetValue().guid);
    }

    Result<LoadedResourceData> RuntimeResourceLoader::LoadResource(const Guid& guid, ResourceSystem& resourceSystem)
    {
        Result<std::vector<ResourceRecord>> loadOrder = ResolveLoadOrder(guid);
        if (!loadOrder)
        {
            return Result<LoadedResourceData>::Failure(loadOrder.GetError());
        }

        Result<LoadedResourceData> rootResource =
            Result<LoadedResourceData>::Failure(Error(ErrorCode::NotFound, "Root resource was not loaded."));
        for (const ResourceRecord& record : loadOrder.GetValue())
        {
            Result<LoadedResourceData> loaded = resourceSystem.LoadResource(record);
            if (!loaded)
            {
                return loaded;
            }

            if (record.guid == guid)
            {
                rootResource = std::move(loaded);
            }
        }

        return rootResource;
    }

    Result<LoadedResourceData> RuntimeResourceLoader::LoadResource(const Path& runtimePath,
                                                                   ResourceSystem& resourceSystem)
    {
        Result<ResourceRecord> record = FindResource(runtimePath);
        if (!record)
        {
            return Result<LoadedResourceData>::Failure(record.GetError());
        }

        return LoadResource(record.GetValue().guid, resourceSystem);
    }

    ResourceLoadOperation RuntimeResourceLoader::LoadResourceAsync(const Guid& guid, ResourceSystem& resourceSystem)
    {
        return ResourceLoadOperation(LoadResource(guid, resourceSystem));
    }

    ResourceLoadOperation RuntimeResourceLoader::LoadResourceAsync(const Path& runtimePath,
                                                                   ResourceSystem& resourceSystem)
    {
        return ResourceLoadOperation(LoadResource(runtimePath, resourceSystem));
    }

    Result<std::vector<ResourceRecord>> RuntimeResourceLoader::ResolveLoadOrderRecursive(
        const Guid& guid,
        std::vector<ResourceRecord>& loadOrder,
        std::vector<Guid>& visiting,
        std::vector<Guid>& visited) const
    {
        if (ContainsGuid(visited, guid))
        {
            return Result<std::vector<ResourceRecord>>::Success(loadOrder);
        }

        if (ContainsGuid(visiting, guid))
        {
            return Result<std::vector<ResourceRecord>>::Failure(
                Error(ErrorCode::InvalidState, "Resource dependency cycle detected."));
        }

        Result<ResourceRecord> record = FindResource(guid);
        if (!record)
        {
            return Result<std::vector<ResourceRecord>>::Failure(record.GetError());
        }

        visiting.push_back(guid);
        for (const Guid& dependency : record.GetValue().dependencies)
        {
            Result<std::vector<ResourceRecord>> dependencyResult =
                ResolveLoadOrderRecursive(dependency, loadOrder, visiting, visited);
            if (!dependencyResult)
            {
                return dependencyResult;
            }
        }
        visiting.pop_back();

        visited.push_back(guid);
        loadOrder.push_back(record.GetValue());
        return Result<std::vector<ResourceRecord>>::Success(loadOrder);
    }
} // namespace ve
