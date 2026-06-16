#include "Editor/Core/EditorResourceLoader.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] bool ContainsGuid(const std::vector<Guid>& values, const Guid& guid)
        {
            return std::find(values.begin(), values.end(), guid) != values.end();
        }

        [[nodiscard]] ResourceType ToResourceType(EditorAssetType type) noexcept
        {
            switch (type)
            {
            case EditorAssetType::ObjSource:
                return ResourceType::Mesh;
            case EditorAssetType::Material:
                return ResourceType::Material;
            case EditorAssetType::Scene:
                return ResourceType::Scene;
            case EditorAssetType::Mesh:
            case EditorAssetType::Unknown:
                break;
            }

            return ResourceType::Unknown;
        }
    } // namespace

    ErrorCode EditorResourceLoader::Initialize(Path projectRoot)
    {
        if (projectRoot.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        projectRoot_ = std::move(projectRoot);
        initialized_ = true;
        return ErrorCode::None;
    }

    void EditorResourceLoader::Shutdown() noexcept
    {
        ClearCache();
        projectRoot_ = Path();
        initialized_ = false;
    }

    bool EditorResourceLoader::IsInitialized() const noexcept
    {
        return initialized_;
    }

    const Path& EditorResourceLoader::GetProjectRoot() const noexcept
    {
        return projectRoot_;
    }

    Result<ResourceObject*> EditorResourceLoader::LoadResourceObject(const AssetRecord& rootRecord,
                                                                     const EditorAssetDatabase& assetDatabase)
    {
        if (!initialized_)
        {
            return Result<ResourceObject*>::Failure(
                Error(ErrorCode::InvalidState, "EditorResourceLoader is not initialized."));
        }

        Result<std::vector<AssetRecord>> loadOrder = ResolveLoadOrder(rootRecord.guid, assetDatabase);
        if (!loadOrder)
        {
            return Result<ResourceObject*>::Failure(loadOrder.GetError());
        }

        ResourceObject* rootResource = nullptr;
        for (const AssetRecord& record : loadOrder.GetValue())
        {
            const auto cached = cache_.find(record.guid);
            if (cached != cache_.end())
            {
                if (record.guid == rootRecord.guid)
                {
                    rootResource = cached->second.resource.get();
                }
                continue;
            }

            Result<std::unique_ptr<ResourceObject>> resource = CreateResourceObject(record);
            if (!resource)
            {
                return Result<ResourceObject*>::Failure(resource.GetError());
            }

            LoadedEditorResource entry;
            entry.resource = resource.MoveValue();
            entry.dependencies = record.dependencies;

            ResourceObject* resourcePointer = entry.resource.get();
            cache_.insert_or_assign(record.guid, std::move(entry));
            if (record.guid == rootRecord.guid)
            {
                rootResource = resourcePointer;
            }
        }

        if (rootResource == nullptr)
        {
            return Result<ResourceObject*>::Failure(Error(ErrorCode::NotFound, "Root editor resource was not loaded."));
        }

        return Result<ResourceObject*>::Success(rootResource);
    }

    Result<AssetRecord> EditorResourceLoader::ResolveAssetRecord(
        const Path& projectRelativePath,
        const EditorAssetDatabase& assetDatabase) const
    {
        if (!assetDatabase.IsInitialized())
        {
            return Result<AssetRecord>::Failure(
                Error(ErrorCode::InvalidState, "EditorAssetDatabase is not initialized."));
        }

        if (projectRelativePath.IsEmpty())
        {
            return Result<AssetRecord>::Failure(Error(ErrorCode::InvalidArgument, "Asset path is empty."));
        }

        const EditorAssetRecord* asset = assetDatabase.FindAsset(projectRelativePath);
        if (asset == nullptr)
        {
            return Result<AssetRecord>::Failure(Error(ErrorCode::NotFound, "Asset not found."));
        }

        return Result<AssetRecord>::Success(BuildAssetRecord(*asset));
    }

    Result<AssetRecord> EditorResourceLoader::ResolveAssetRecord(
        const Guid& guid,
        const EditorAssetDatabase& assetDatabase) const
    {
        if (!assetDatabase.IsInitialized())
        {
            return Result<AssetRecord>::Failure(
                Error(ErrorCode::InvalidState, "EditorAssetDatabase is not initialized."));
        }

        if (guid.IsEmpty())
        {
            return Result<AssetRecord>::Failure(Error(ErrorCode::InvalidArgument, "Asset GUID is empty."));
        }

        const EditorAssetRecord* asset = assetDatabase.FindAssetByGuid(guid);
        if (asset == nullptr)
        {
            return Result<AssetRecord>::Failure(Error(ErrorCode::NotFound, "Asset not found."));
        }

        return Result<AssetRecord>::Success(BuildAssetRecord(*asset));
    }

    Result<std::vector<AssetRecord>> EditorResourceLoader::ResolveLoadOrder(
        const Guid& guid,
        const EditorAssetDatabase& assetDatabase) const
    {
        std::vector<AssetRecord> loadOrder;
        std::vector<Guid> visiting;
        std::vector<Guid> visited;
        return ResolveLoadOrderRecursive(guid, assetDatabase, loadOrder, visiting, visited);
    }

    Result<std::vector<AssetRecord>> EditorResourceLoader::ResolveLoadOrderRecursive(
        const Guid& guid,
        const EditorAssetDatabase& assetDatabase,
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
                Error(ErrorCode::InvalidState, "Editor resource dependency cycle detected."));
        }

        Result<AssetRecord> record = ResolveAssetRecord(guid, assetDatabase);
        if (!record)
        {
            return Result<std::vector<AssetRecord>>::Failure(record.GetError());
        }

        visiting.push_back(guid);
        for (const Guid& dependency : record.GetValue().dependencies)
        {
            Result<std::vector<AssetRecord>> dependencyResult =
                ResolveLoadOrderRecursive(dependency, assetDatabase, loadOrder, visiting, visited);
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

    AssetRecord EditorResourceLoader::BuildAssetRecord(const EditorAssetRecord& asset) const
    {
        AssetRecord record;
        record.guid = asset.guid;
        record.type = ToResourceType(asset.type);
        record.runtimePath = asset.imported ? asset.importedPath : asset.path;
        record.dependencies = asset.dependencies;
        return record;
    }

    SizeT EditorResourceLoader::CollectUnusedResources(const EditorResourceCollectUnusedParams& params)
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
            if (reachable)
            {
                ++it;
                continue;
            }

            it = cache_.erase(it);
            ++unloadedCount;
        }

        return unloadedCount;
    }

    void EditorResourceLoader::ClearCache() noexcept
    {
        cache_.clear();
    }

    Path EditorResourceLoader::ResolveRuntimePath(const AssetRecord& record) const
    {
        if (record.runtimePath.IsEmpty())
        {
            return Path();
        }

        return projectRoot_ / record.runtimePath;
    }

    Result<std::unique_ptr<ResourceObject>> EditorResourceLoader::CreateResourceObject(
        const AssetRecord& record) const
    {
        const Path physicalPath = ResolveRuntimePath(record);
        if (physicalPath.IsEmpty())
        {
            return Result<std::unique_ptr<ResourceObject>>::Failure(
                Error(ErrorCode::InvalidArgument, "Editor resource runtime path is empty."));
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
            Error(ErrorCode::Unsupported, "Unsupported editor resource type."));
    }

    void EditorResourceLoader::MarkReachableResource(const Guid& guid, std::vector<Guid>& reachableResources) const
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
} // namespace ve::editor
