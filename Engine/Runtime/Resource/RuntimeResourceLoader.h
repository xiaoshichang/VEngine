#pragma once

#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetManifest.h"
#include "Engine/Runtime/Resource/ResourceObject.h"
#include "Engine/Runtime/Resource/ResourceSystem.h"

#include <vector>

namespace ve
{
    struct RuntimeResourceLoaderInitParam
    {
        Path projectRoot;
        Path assetManifestPath;
    };

    class RuntimeResourceLoader : public NonMovable
    {
    public:
        RuntimeResourceLoader() = default;
        ~RuntimeResourceLoader() = default;

        [[nodiscard]] ErrorCode Initialize(const RuntimeResourceLoaderInitParam& desc);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;
        [[nodiscard]] const Path& GetAssetManifestPath() const noexcept;
        [[nodiscard]] const AssetManifest& GetAssetManifest() const noexcept;

        template<typename TResource>
        [[nodiscard]] Result<TResource*> LoadResource(const Path& runtimePath, ResourceSystem& resourceSystem);

        [[nodiscard]] ErrorCode ReleaseResource(ResourceObject* resource, ResourceSystem& resourceSystem);

    private:
        [[nodiscard]] Result<AssetRecord> FindAsset(const Guid& guid) const;
        [[nodiscard]] Result<AssetRecord> FindAsset(const Path& runtimePath) const;
        [[nodiscard]] Result<std::vector<AssetRecord>> ResolveLoadOrder(const Guid& guid) const;
        [[nodiscard]] Result<ResourceObject*> LoadResourceObject(const AssetRecord& rootRecord,
                                                                 ResourceSystem& resourceSystem);
        [[nodiscard]] Result<std::vector<AssetRecord>> ResolveLoadOrderRecursive(
            const Guid& guid,
            std::vector<AssetRecord>& loadOrder,
            std::vector<Guid>& visiting,
            std::vector<Guid>& visited) const;

        Path projectRoot_;
        Path assetManifestPath_;
        AssetManifest assetManifest_;
        bool initialized_ = false;
    };

    template<typename TResource>
    Result<TResource*> RuntimeResourceLoader::LoadResource(const Path& runtimePath, ResourceSystem& resourceSystem)
    {
        Result<AssetRecord> record = FindAsset(runtimePath);
        if (!record)
        {
            return Result<TResource*>::Failure(record.GetError());
        }

        if (record.GetValue().type != ResourceObjectTraits<TResource>::Type)
        {
            return Result<TResource*>::Failure(
                Error(ErrorCode::InvalidArgument, "Requested resource type does not match the runtime path."));
        }

        Result<ResourceObject*> resource = LoadResourceObject(record.GetValue(), resourceSystem);
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
