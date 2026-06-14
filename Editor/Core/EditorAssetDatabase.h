#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/ResourceManifest.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace ve
{
    class ResourceSystem;
}

namespace ve::editor
{
    enum class EditorAssetType
    {
        Unknown,
        ObjSource,
        Mesh,
        Material,
        Scene,
    };

    struct EditorAssetRecord
    {
        Path path;
        Path metaPath;
        Guid guid;
        EditorAssetType type = EditorAssetType::Unknown;
        bool imported = false;
        Path importedPath;
    };

    class EditorAssetDatabase : public NonMovable
    {
    public:
        EditorAssetDatabase() = default;
        ~EditorAssetDatabase() = default;

        [[nodiscard]] ErrorCode Initialize(const Path& projectRoot);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;
        [[nodiscard]] Path GetAssetsRootPath() const;

        [[nodiscard]] ErrorCode Refresh();
        [[nodiscard]] ErrorCode ReimportAll();
        [[nodiscard]] ErrorCode ReimportAsset(const Path& projectRelativePath);
        void RegisterResourceSystemCallbacks(ve::ResourceSystem& resourceSystem);
        [[nodiscard]] SizeT GetAssetCount() const noexcept;
        [[nodiscard]] const EditorAssetRecord* FindAsset(const Path& projectRelativePath) const noexcept;
        [[nodiscard]] const EditorAssetRecord* FindAssetByGuid(const Guid& guid) const noexcept;
        [[nodiscard]] const std::unordered_map<std::string, EditorAssetRecord>& GetAssets() const noexcept;

        [[nodiscard]] static const char* ToString(EditorAssetType type) noexcept;

    private:
        [[nodiscard]] ErrorCode ScanAndImportDirectory(const Path& physicalDirectoryPath, bool force);
        [[nodiscard]] ErrorCode ImportObjAsMesh(const Path& objProjectPath, const Guid& guid, bool force);
        [[nodiscard]] Result<Guid> EnsureMeta(const EditorAssetRecord& record) const;
        [[nodiscard]] Result<Guid> ReadMetaGuid(const Path& metaPhysicalPath) const;
        [[nodiscard]] Path GetImportedMeshPath(const Guid& guid, const Path& objProjectPath) const;
        [[nodiscard]] Path GetMetaPath(const Path& assetProjectPath) const;
        [[nodiscard]] Path ToProjectRelativePath(const Path& physicalPath) const;
        void AddAssetRecord(EditorAssetRecord record);
        [[nodiscard]] ResourceRecord BuildResourceRecord(const EditorAssetRecord& asset) const;

        Path projectRoot_;
        std::unordered_map<std::string, EditorAssetRecord> assets_;
        std::unordered_map<Guid, std::string> assetPathsByGuid_;
        bool initialized_ = false;
    };
} // namespace ve::editor
