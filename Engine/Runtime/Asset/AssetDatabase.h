#pragma once

#include "Engine/Runtime/Asset/AssetGuid.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>
#include <string_view>
#include <vector>

namespace ve
{
    enum class AssetType
    {
        Unknown,
        SourceModel,
        Scene,
        Material,
        Mesh,
    };

    [[nodiscard]] const char* ToString(AssetType type) noexcept;
    [[nodiscard]] AssetType ParseAssetType(std::string_view text) noexcept;

    struct AssetArtifact
    {
        std::string type;
        Path path;
    };

    struct AssetDependency
    {
        std::string type;
        Path path;
        AssetGuid guid;
    };

    struct SourceAssetMetadata
    {
        AssetGuid guid;
        AssetType assetType = AssetType::Unknown;
        Path source;
        std::string sourceHash;
        std::string importer;
        UInt32 importerVersion = 1;
        std::vector<AssetArtifact> artifacts;
        std::vector<AssetDependency> dependencies;
    };

    struct AssetRecord
    {
        AssetGuid guid;
        AssetType assetType = AssetType::Unknown;
        Path path;
        Path metadataPath;
        Path source;
        std::vector<AssetArtifact> artifacts;
        std::vector<AssetDependency> dependencies;
    };

    class AssetDatabase
    {
    public:
        [[nodiscard]] ErrorCode Open(Path projectRoot);
        [[nodiscard]] ErrorCode Refresh();
        [[nodiscard]] ErrorCode Validate() const;

        [[nodiscard]] const Path& GetProjectRoot() const noexcept;
        [[nodiscard]] const std::vector<AssetRecord>& GetRecords() const noexcept;
        [[nodiscard]] const AssetRecord* FindAsset(const AssetGuid& guid) const noexcept;
        [[nodiscard]] const AssetRecord* FindAssetByPath(const Path& projectRelativePath) const noexcept;
        [[nodiscard]] Result<Path> ResolveArtifact(const AssetGuid& guid, std::string_view artifactType) const;

        [[nodiscard]] Path MakeProjectRelativePath(const Path& path) const;
        [[nodiscard]] Path ResolveProjectPath(const Path& projectRelativePath) const;

        [[nodiscard]] Result<SourceAssetMetadata> LoadSourceMetadata(const Path& metadataPath) const;
        [[nodiscard]] ErrorCode SaveSourceMetadata(const SourceAssetMetadata& metadata) const;
        [[nodiscard]] Path GetSourceMetadataPath(const Path& sourcePath) const;

    private:
        [[nodiscard]] ErrorCode ScanDirectory(const Path& path);
        [[nodiscard]] ErrorCode AddRecord(AssetRecord record);
        [[nodiscard]] ErrorCode AddSourceAssetCandidateRecord(const Path& path);
        [[nodiscard]] ErrorCode AddNativeAssetRecord(const Path& path);

        Path projectRoot_;
        std::vector<AssetRecord> records_;
        std::vector<std::string> diagnostics_;
    };
} // namespace ve
