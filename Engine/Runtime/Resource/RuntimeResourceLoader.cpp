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
        if (desc.assetManifestPath.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        projectRoot_ = desc.projectRoot;
        assetManifestPath_ = desc.assetManifestPath;
        assetManifest_.Clear();

        const ErrorCode loadResult = assetManifest_.LoadFromFile(assetManifestPath_);
        if (loadResult != ErrorCode::None)
        {
            projectRoot_ = Path();
            assetManifestPath_ = Path();
            return loadResult;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void RuntimeResourceLoader::Shutdown() noexcept
    {
        assetManifest_.Clear();
        projectRoot_ = Path();
        assetManifestPath_ = Path();
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

    const Path& RuntimeResourceLoader::GetAssetManifestPath() const noexcept
    {
        return assetManifestPath_;
    }

    const AssetManifest& RuntimeResourceLoader::GetAssetManifest() const noexcept
    {
        return assetManifest_;
    }

    Result<AssetRecord> RuntimeResourceLoader::FindAsset(const Guid& guid) const
    {
        if (!initialized_)
        {
            return Result<AssetRecord>::Failure(
                Error(ErrorCode::InvalidState, "RuntimeResourceLoader is not initialized."));
        }

        if (guid.IsEmpty())
        {
            return Result<AssetRecord>::Failure(Error(ErrorCode::InvalidArgument, "Asset GUID is empty."));
        }

        const AssetRecord* record = assetManifest_.Find(guid);
        if (record == nullptr)
        {
            return Result<AssetRecord>::Failure(
                Error(ErrorCode::NotFound, "Asset not found in runtime asset manifest."));
        }

        return Result<AssetRecord>::Success(*record);
    }

    Result<AssetRecord> RuntimeResourceLoader::FindAsset(const Path& runtimePath) const
    {
        if (!initialized_)
        {
            return Result<AssetRecord>::Failure(
                Error(ErrorCode::InvalidState, "RuntimeResourceLoader is not initialized."));
        }

        if (runtimePath.IsEmpty())
        {
            return Result<AssetRecord>::Failure(Error(ErrorCode::InvalidArgument, "Asset path is empty."));
        }

        const AssetRecord* record = assetManifest_.FindByRuntimePath(runtimePath);
        if (record == nullptr)
        {
            return Result<AssetRecord>::Failure(
                Error(ErrorCode::NotFound, "Asset path not found in runtime asset manifest."));
        }

        return Result<AssetRecord>::Success(*record);
    }

    Result<std::vector<AssetRecord>> RuntimeResourceLoader::ResolveLoadOrder(const Guid& guid) const
    {
        std::vector<AssetRecord> loadOrder;
        std::vector<Guid> visiting;
        std::vector<Guid> visited;
        return ResolveLoadOrderRecursive(guid, loadOrder, visiting, visited);
    }

    Result<ResourceObject*> RuntimeResourceLoader::LoadResourceObject(const AssetRecord& rootRecord,
                                                                      ResourceSystem& resourceSystem)
    {
        Result<std::vector<AssetRecord>> loadOrder = ResolveLoadOrder(rootRecord.guid);
        if (!loadOrder)
        {
            return Result<ResourceObject*>::Failure(loadOrder.GetError());
        }

        ResourceObject* rootResource = nullptr;
        std::vector<ResourceObject*> loadedResources;
        for (const AssetRecord& record : loadOrder.GetValue())
        {
            Result<ResourceObject*> loaded = resourceSystem.LoadResource(record);
            if (!loaded)
            {
                for (auto it = loadedResources.rbegin(); it != loadedResources.rend(); ++it)
                {
                    (void)resourceSystem.ReleaseResource(*it);
                }
                return loaded;
            }

            loadedResources.push_back(loaded.GetValue());
            if (record.guid == rootRecord.guid)
            {
                rootResource = loaded.GetValue();
            }
        }

        if (rootResource == nullptr)
        {
            for (auto it = loadedResources.rbegin(); it != loadedResources.rend(); ++it)
            {
                (void)resourceSystem.ReleaseResource(*it);
            }
            return Result<ResourceObject*>::Failure(Error(ErrorCode::NotFound, "Root resource was not loaded."));
        }

        return Result<ResourceObject*>::Success(rootResource);
    }

    ErrorCode RuntimeResourceLoader::ReleaseResource(ResourceObject* resource, ResourceSystem& resourceSystem)
    {
        return resourceSystem.ReleaseResource(resource);
    }

    Result<std::vector<AssetRecord>> RuntimeResourceLoader::ResolveLoadOrderRecursive(
        const Guid& guid,
        std::vector<AssetRecord>& loadOrder,
        std::vector<Guid>& visiting,
        std::vector<Guid>& visited) const
    {
        if (ContainsGuid(visited, guid))
        {
            return Result<std::vector<AssetRecord>>::Success(loadOrder);
        }

        if (ContainsGuid(visiting, guid))
        {
            return Result<std::vector<AssetRecord>>::Failure(
                Error(ErrorCode::InvalidState, "Resource dependency cycle detected."));
        }

        Result<AssetRecord> record = FindAsset(guid);
        if (!record)
        {
            return Result<std::vector<AssetRecord>>::Failure(record.GetError());
        }

        visiting.push_back(guid);
        for (const Guid& dependency : record.GetValue().dependencies)
        {
            Result<std::vector<AssetRecord>> dependencyResult =
                ResolveLoadOrderRecursive(dependency, loadOrder, visiting, visited);
            if (!dependencyResult)
            {
                return dependencyResult;
            }
        }
        visiting.pop_back();

        visited.push_back(guid);
        loadOrder.push_back(record.GetValue());
        return Result<std::vector<AssetRecord>>::Success(loadOrder);
    }
} // namespace ve
