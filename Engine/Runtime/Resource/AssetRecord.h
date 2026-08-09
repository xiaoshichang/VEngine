#pragma once

#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetID.h"

#include <string>
#include <vector>

namespace ve
{
    enum class ResourceType
    {
        Unknown,
        Scene,
        Mesh,
        Material,
        Texture,
        Shader,
    };

    struct AssetRecord
    {
        AssetID id;
        ResourceType type = ResourceType::Unknown;
        Path runtimePath;
        // Editor-only physical source path for repository-owned builtin assets.
        // This is intentionally omitted from runtime manifests.
        Path sourcePathOverride;
        std::vector<AssetID> dependencies;
        std::string contentHash;
    };

    struct ManifestAssetRecord
    {
        AssetRecord asset;
        std::string bundle;
    };

    class IAssetRecordProvider
    {
    public:
        virtual ~IAssetRecordProvider() = default;

        [[nodiscard]] virtual Result<AssetRecord> FindAssetRecord(const AssetID& id) const = 0;
        [[nodiscard]] virtual Result<AssetID> FindAssetIDByRuntimePath(const Path& runtimePath) const = 0;
    };
} // namespace ve
