#pragma once

#include "Editor/Core/EditorAssetDatabase.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetRef.h"
#include "Engine/Runtime/Resource/ResourceSystem.h"

namespace ve::editor
{
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
        [[nodiscard]] Result<AssetRef<TResource>> LoadResource(const AssetID& id, const EditorAssetDatabase& assetDatabase, ResourceSystem& resourceSystem);

        template<typename TResource>
        [[nodiscard]] Result<AssetRef<TResource>>
        LoadResource(const Path& projectRelativePath, const EditorAssetDatabase& assetDatabase, ResourceSystem& resourceSystem);

    private:
        Path projectRoot_;
        bool initialized_ = false;
    };

    template<typename TResource>
    Result<AssetRef<TResource>> EditorResourceLoader::LoadResource(const AssetID& id, const EditorAssetDatabase& assetDatabase, ResourceSystem& resourceSystem)
    {
        if (!initialized_)
        {
            return Result<AssetRef<TResource>>::Failure(Error(ErrorCode::InvalidState, "EditorResourceLoader is not initialized."));
        }

        return resourceSystem.Request<TResource>(id, assetDatabase);
    }

    template<typename TResource>
    Result<AssetRef<TResource>>
    EditorResourceLoader::LoadResource(const Path& projectRelativePath, const EditorAssetDatabase& assetDatabase, ResourceSystem& resourceSystem)
    {
        if (!initialized_)
        {
            return Result<AssetRef<TResource>>::Failure(Error(ErrorCode::InvalidState, "EditorResourceLoader is not initialized."));
        }

        const EditorAssetRecord* asset = assetDatabase.FindAsset(projectRelativePath);
        if (asset == nullptr)
        {
            return Result<AssetRef<TResource>>::Failure(Error(ErrorCode::NotFound, "Editor asset not found."));
        }

        return resourceSystem.Request<TResource>(asset->asset.id, assetDatabase);
    }
} // namespace ve::editor
