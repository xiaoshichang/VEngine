#pragma once

#include "Editor/Core/EditorAssetDatabase.h"
#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetManifest.h"
#include "Engine/Runtime/Resource/ResourceObject.h"

#include <memory>
#include <unordered_map>
#include <vector>

namespace ve::editor
{
    struct EditorResourceCollectUnusedParams
    {
        std::vector<Guid> rootGuids;
    };

    class EditorResourceLoader : public NonMovable
    {
    public:
        EditorResourceLoader() = default;
        ~EditorResourceLoader() = default;

        [[nodiscard]] ErrorCode Initialize(Path projectRoot);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;

        template<typename TResource>
        [[nodiscard]] Result<TResource*> LoadResource(const Path& projectRelativePath,
                                                      const EditorAssetDatabase& assetDatabase);

        [[nodiscard]] SizeT CollectUnusedResources(const EditorResourceCollectUnusedParams& params);
        void ClearCache() noexcept;

    private:
        struct LoadedEditorResource
        {
            std::unique_ptr<ResourceObject> resource;
            std::vector<Guid> dependencies;
        };

        [[nodiscard]] Result<ResourceObject*> LoadResourceObject(const AssetRecord& rootRecord,
                                                                 const EditorAssetDatabase& assetDatabase);
        [[nodiscard]] Result<AssetRecord> ResolveAssetRecord(
            const Path& projectRelativePath,
            const EditorAssetDatabase& assetDatabase) const;
        [[nodiscard]] Result<AssetRecord> ResolveAssetRecord(
            const Guid& guid,
            const EditorAssetDatabase& assetDatabase) const;
        [[nodiscard]] Result<std::vector<AssetRecord>> ResolveLoadOrder(
            const Guid& guid,
            const EditorAssetDatabase& assetDatabase) const;
        [[nodiscard]] Result<std::vector<AssetRecord>> ResolveLoadOrderRecursive(
            const Guid& guid,
            const EditorAssetDatabase& assetDatabase,
            std::vector<AssetRecord>& loadOrder,
            std::vector<Guid>& visiting,
            std::vector<Guid>& visited) const;
        [[nodiscard]] AssetRecord BuildAssetRecord(const EditorAssetRecord& asset) const;
        [[nodiscard]] Path ResolveRuntimePath(const AssetRecord& record) const;
        [[nodiscard]] Result<std::unique_ptr<ResourceObject>> CreateResourceObject(const AssetRecord& record) const;
        void MarkReachableResource(const Guid& guid, std::vector<Guid>& reachableResources) const;

        Path projectRoot_;
        std::unordered_map<Guid, LoadedEditorResource> cache_;
        bool initialized_ = false;
    };

    template<typename TResource>
    Result<TResource*> EditorResourceLoader::LoadResource(const Path& projectRelativePath,
                                                          const EditorAssetDatabase& assetDatabase)
    {
        Result<AssetRecord> record = ResolveAssetRecord(projectRelativePath, assetDatabase);
        if (!record)
        {
            return Result<TResource*>::Failure(record.GetError());
        }

        if (record.GetValue().type != ResourceObjectTraits<TResource>::Type)
        {
            return Result<TResource*>::Failure(
                Error(ErrorCode::InvalidArgument, "Requested resource type does not match the editor asset path."));
        }

        Result<ResourceObject*> resource = LoadResourceObject(record.GetValue(), assetDatabase);
        if (!resource)
        {
            return Result<TResource*>::Failure(resource.GetError());
        }

        TResource* typedResource = dynamic_cast<TResource*>(resource.GetValue());
        if (typedResource == nullptr)
        {
            return Result<TResource*>::Failure(
                Error(ErrorCode::InvalidState, "Loaded editor resource has an unexpected concrete type."));
        }

        return Result<TResource*>::Success(typedResource);
    }
} // namespace ve::editor
