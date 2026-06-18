#include "Engine/Runtime/Resource/RuntimeAssetLoader.h"

#include <utility>

namespace ve
{
    ErrorCode RuntimeAssetLoader::Initialize(const RuntimeAssetLoaderInitParam& desc)
    {
        if (desc.assetManifestPath.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        assetManifestPath_ = desc.assetManifestPath;
        assetManifest_.Clear();

        const ErrorCode loadResult = assetManifest_.LoadFromFile(assetManifestPath_);
        if (loadResult != ErrorCode::None)
        {
            assetManifestPath_ = Path();
            return loadResult;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void RuntimeAssetLoader::Shutdown() noexcept
    {
        assetManifest_.Clear();
        assetManifestPath_ = Path();
        initialized_ = false;
    }

    bool RuntimeAssetLoader::IsInitialized() const noexcept
    {
        return initialized_;
    }

    const Path& RuntimeAssetLoader::GetAssetManifestPath() const noexcept
    {
        return assetManifestPath_;
    }

    const AssetManifest& RuntimeAssetLoader::GetAssetManifest() const noexcept
    {
        return assetManifest_;
    }

    Result<AssetRecord> RuntimeAssetLoader::FindAssetRecord(const AssetID& id) const
    {
        Result<ManifestAssetRecord> record = FindManifestAssetRecord(id);
        if (!record)
        {
            return Result<AssetRecord>::Failure(record.GetError());
        }

        return Result<AssetRecord>::Success(record.GetValue().asset);
    }

    Result<ManifestAssetRecord> RuntimeAssetLoader::FindManifestAssetRecord(const AssetID& id) const
    {
        if (!initialized_)
        {
            return Result<ManifestAssetRecord>::Failure(
                Error(ErrorCode::InvalidState, "RuntimeAssetLoader is not initialized."));
        }

        if (id.IsEmpty())
        {
            return Result<ManifestAssetRecord>::Failure(Error(ErrorCode::InvalidArgument, "AssetID is empty."));
        }

        const ManifestAssetRecord* record = assetManifest_.Find(id);
        if (record == nullptr)
        {
            return Result<ManifestAssetRecord>::Failure(
                Error(ErrorCode::NotFound, "Asset not found in runtime asset manifest."));
        }

        return Result<ManifestAssetRecord>::Success(*record);
    }

    Result<AssetID> RuntimeAssetLoader::FindAssetIDByRuntimePath(const Path& runtimePath) const
    {
        if (!initialized_)
        {
            return Result<AssetID>::Failure(Error(ErrorCode::InvalidState, "RuntimeAssetLoader is not initialized."));
        }

        if (runtimePath.IsEmpty())
        {
            return Result<AssetID>::Failure(Error(ErrorCode::InvalidArgument, "Asset path is empty."));
        }

        const ManifestAssetRecord* record = assetManifest_.FindByRuntimePath(runtimePath);
        if (record == nullptr)
        {
            return Result<AssetID>::Failure(Error(ErrorCode::NotFound, "Asset path not found in runtime manifest."));
        }

        return Result<AssetID>::Success(record->asset.id);
    }
} // namespace ve
