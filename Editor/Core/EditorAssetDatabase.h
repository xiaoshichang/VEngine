#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Guid.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Resource/AssetRecord.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ve::editor
{
    enum class EditorAssetType
    {
        Unknown,
        ObjSource,
        Mesh,
        Material,
        Shader,
        Scene,
        Script,
    };

    struct EditorAssetRecord
    {
        AssetRecord asset;
        Path path;
        Path metaPath;
        EditorAssetType type = EditorAssetType::Unknown;
        bool imported = false;
        Path importedPath;
    };

    class EditorAssetDatabase : public IAssetRecordProvider, public NonMovable
    {
    public:
        EditorAssetDatabase() = default;
        ~EditorAssetDatabase() = default;

        [[nodiscard]] ErrorCode Initialize(const Path& projectRoot);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] const Path& GetProjectRoot() const noexcept;
        [[nodiscard]] Path GetAssetsRootPath() const;
        [[nodiscard]] Path GetBuiltinAssetsRootPath() const;

        [[nodiscard]] ErrorCode Refresh();
        [[nodiscard]] ErrorCode ReimportAll();
        [[nodiscard]] ErrorCode ReimportAsset(const Path& projectRelativePath);
        [[nodiscard]] SizeT GetAssetCount() const noexcept;
        [[nodiscard]] const EditorAssetRecord* FindAsset(const Path& projectRelativePath) const noexcept;
        [[nodiscard]] const EditorAssetRecord* FindAssetByID(const AssetID& id) const noexcept;
        [[nodiscard]] Result<AssetRecord> FindAssetRecord(const AssetID& id) const override;
        [[nodiscard]] const std::unordered_map<AssetID, EditorAssetRecord>& GetAssetsByID() const noexcept;
        [[nodiscard]] const std::unordered_map<std::string, AssetID>& GetAssetIDsByAssetPath() const noexcept;

        [[nodiscard]] static const char* ToString(EditorAssetType type) noexcept;

    private:
        [[nodiscard]] ErrorCode ScanAndImportDirectory(const Path& physicalDirectoryPath, bool force);
        [[nodiscard]] ErrorCode ImportObjAsMesh(const Path& objProjectPath, const Guid& guid, bool force);
        [[nodiscard]] ErrorCode ImportShader(const Path& shaderProjectPath, const Guid& guid, bool force);
        [[nodiscard]] ErrorCode ResolveAssetDependencies();
        [[nodiscard]] ErrorCode RegenerateScriptProject() const;
        [[nodiscard]] Result<Guid> EnsureMeta(const EditorAssetRecord& record) const;
        [[nodiscard]] Result<Guid> ReadMetaGuid(const Path& metaPhysicalPath) const;
        [[nodiscard]] Path GetImportedMeshPath(const Guid& guid, const Path& objProjectPath) const;
        [[nodiscard]] Path GetImportedShaderDirectory(const Guid& guid) const;
        [[nodiscard]] Path GetImportedShaderPath(const Guid& guid, std::string_view shaderName) const;
        [[nodiscard]] Result<std::string> ReadShaderName(const Path& shaderProjectPath) const;
        [[nodiscard]] Path GetMetaPath(const Path& assetProjectPath) const;
        [[nodiscard]] Path ToProjectRelativePath(const Path& physicalPath) const;
        void AddAssetRecord(EditorAssetRecord record);

        Path projectRoot_;
        std::unordered_map<AssetID, EditorAssetRecord> assetsByID_;
        std::unordered_map<std::string, AssetID> assetIDsByAssetPath_;
        bool initialized_ = false;
    };
} // namespace ve::editor
