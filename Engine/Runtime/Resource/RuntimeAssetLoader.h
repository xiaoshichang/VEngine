#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetManifest.h"
#include "Engine/Runtime/Resource/AssetRecord.h"

namespace ve
{
    struct RuntimeAssetLoaderInitParam
    {
        Path assetManifestPath;
    };

    class RuntimeAssetLoader : public IAssetRecordProvider, public NonMovable
    {
    public:
        RuntimeAssetLoader() = default;
        ~RuntimeAssetLoader() = default;

        [[nodiscard]] ErrorCode Initialize(const RuntimeAssetLoaderInitParam& desc);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const Path& GetAssetManifestPath() const noexcept;
        [[nodiscard]] const AssetManifest& GetAssetManifest() const noexcept;

        [[nodiscard]] Result<AssetRecord> FindAssetRecord(const AssetID& id) const override;
        [[nodiscard]] Result<ManifestAssetRecord> FindManifestAssetRecord(const AssetID& id) const;
        [[nodiscard]] Result<AssetID> FindAssetIDByRuntimePath(const Path& runtimePath) const;

    private:
        Path assetManifestPath_;
        AssetManifest assetManifest_;
        bool initialized_ = false;
    };
} // namespace ve
