#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>
#include <vector>

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
        EditorAssetType type = EditorAssetType::Unknown;
        bool imported = false;
        Path sourcePath;
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
        [[nodiscard]] SizeT GetAssetCount() const noexcept;
        [[nodiscard]] const EditorAssetRecord* GetAsset(SizeT index) const noexcept;
        [[nodiscard]] const EditorAssetRecord* FindAsset(const Path& projectRelativePath) const noexcept;
        [[nodiscard]] const std::vector<EditorAssetRecord>& GetAssets() const noexcept;

        [[nodiscard]] static const char* ToString(EditorAssetType type) noexcept;

    private:
        [[nodiscard]] ErrorCode ScanAndImportDirectory(const Path& physicalDirectoryPath, bool force);
        [[nodiscard]] ErrorCode ScanRecordsDirectory(const Path& physicalDirectoryPath);
        [[nodiscard]] ErrorCode ImportObjAsMesh(const Path& objProjectPath, bool force);
        [[nodiscard]] Result<Path> ReadMeshSourcePath(const Path& meshPhysicalPath) const;
        [[nodiscard]] Path ToProjectRelativePath(const Path& physicalPath) const;
        void AddAssetRecord(EditorAssetRecord record);

        Path projectRoot_;
        std::vector<EditorAssetRecord> assets_;
        bool initialized_ = false;
    };
} // namespace ve::editor
