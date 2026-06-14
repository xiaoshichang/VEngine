#include "Editor/Core/EditorAssetDatabase.h"

#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <boost/json.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace ve::editor
{
    namespace
    {
        [[nodiscard]] std::string ToLowerCopy(std::string text)
        {
            std::transform(text.begin(),
                           text.end(),
                           text.begin(),
                           [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return text;
        }

        [[nodiscard]] EditorAssetType GetAssetTypeFromExtension(const Path& path)
        {
            const std::string extension = ToLowerCopy(path.GetExtension());
            if (extension == ".obj")
            {
                return EditorAssetType::ObjSource;
            }

            if (extension == ".vemesh")
            {
                return EditorAssetType::Mesh;
            }

            if (extension == ".vematerial")
            {
                return EditorAssetType::Material;
            }

            if (extension == ".vescene")
            {
                return EditorAssetType::Scene;
            }

            return EditorAssetType::Unknown;
        }

        [[nodiscard]] bool IsSupportedAssetFile(const Path& path)
        {
            return GetAssetTypeFromExtension(path) != EditorAssetType::Unknown;
        }

        [[nodiscard]] Path ReplaceExtension(const Path& path, std::string_view extension)
        {
            std::string text = path.GetString();
            const std::string filename = path.GetFilename();
            const size_t filenameStart = text.size() - filename.size();
            const size_t dot = filename.find_last_of('.');
            if (dot == std::string::npos || dot == 0)
            {
                text += extension;
            }
            else
            {
                text.resize(filenameStart + dot);
                text += extension;
            }

            return Path(text);
        }

        [[nodiscard]] boost::json::object BuildMeshAssetJson(const Path& objProjectPath)
        {
            boost::json::object object;
            object["schemaVersion"] = 1;
            object["type"] = "Mesh";
            object["sourceAsset"] = objProjectPath.GetString();
            object["importer"] = "ObjMeshImporter";
            object["importSettings"] = boost::json::object{};
            return object;
        }

        [[nodiscard]] std::string ReadString(const boost::json::object& object,
                                             boost::json::string_view key,
                                             std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }
    } // namespace

    ErrorCode EditorAssetDatabase::Initialize(const Path& projectRoot)
    {
        if (projectRoot.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        projectRoot_ = projectRoot;
        initialized_ = true;
        return Refresh();
    }

    void EditorAssetDatabase::Shutdown() noexcept
    {
        assets_.clear();
        projectRoot_ = Path();
        initialized_ = false;
    }

    bool EditorAssetDatabase::IsInitialized() const noexcept
    {
        return initialized_;
    }

    const Path& EditorAssetDatabase::GetProjectRoot() const noexcept
    {
        return projectRoot_;
    }

    Path EditorAssetDatabase::GetAssetsRootPath() const
    {
        return EditorProject::GetAssetsPath(projectRoot_);
    }

    ErrorCode EditorAssetDatabase::Refresh()
    {
        if (!initialized_)
        {
            return ErrorCode::InvalidState;
        }

        const Path assetsRoot = GetAssetsRootPath();
        if (!FileSystem::IsDirectory(assetsRoot))
        {
            return ErrorCode::NotFound;
        }

        assets_.clear();

        ErrorCode result = ScanAndImportDirectory(assetsRoot, false);
        if (result != ErrorCode::None)
        {
            return result;
        }

        assets_.clear();
        result = ScanRecordsDirectory(assetsRoot);
        if (result != ErrorCode::None)
        {
            return result;
        }

        std::sort(assets_.begin(),
                  assets_.end(),
                  [](const EditorAssetRecord& left, const EditorAssetRecord& right)
                  { return left.path.GetString() < right.path.GetString(); });
        return ErrorCode::None;
    }

    SizeT EditorAssetDatabase::GetAssetCount() const noexcept
    {
        return assets_.size();
    }

    ErrorCode EditorAssetDatabase::ReimportAll()
    {
        if (!initialized_)
        {
            return ErrorCode::InvalidState;
        }

        const Path assetsRoot = GetAssetsRootPath();
        if (!FileSystem::IsDirectory(assetsRoot))
        {
            return ErrorCode::NotFound;
        }

        ErrorCode result = ScanAndImportDirectory(assetsRoot, true);
        if (result != ErrorCode::None)
        {
            return result;
        }

        return Refresh();
    }

    ErrorCode EditorAssetDatabase::ReimportAsset(const Path& projectRelativePath)
    {
        if (!initialized_)
        {
            return ErrorCode::InvalidState;
        }

        const EditorAssetRecord* asset = FindAsset(projectRelativePath);
        if (asset == nullptr)
        {
            return ErrorCode::NotFound;
        }

        ErrorCode result = ErrorCode::None;
        switch (asset->type)
        {
        case EditorAssetType::ObjSource:
            result = ImportObjAsMesh(asset->path, true);
            break;
        case EditorAssetType::Mesh:
            result =
                asset->sourcePath.IsEmpty() ? ErrorCode::InvalidArgument : ImportObjAsMesh(asset->sourcePath, true);
            break;
        case EditorAssetType::Material:
        case EditorAssetType::Scene:
        case EditorAssetType::Unknown:
            result = ErrorCode::None;
            break;
        }

        if (result != ErrorCode::None)
        {
            return result;
        }

        return Refresh();
    }

    const EditorAssetRecord* EditorAssetDatabase::GetAsset(SizeT index) const noexcept
    {
        if (index >= assets_.size())
        {
            return nullptr;
        }

        return &assets_[index];
    }

    const EditorAssetRecord* EditorAssetDatabase::FindAsset(const Path& projectRelativePath) const noexcept
    {
        const auto it = std::find_if(assets_.begin(),
                                     assets_.end(),
                                     [&projectRelativePath](const EditorAssetRecord& record)
                                     { return record.path == projectRelativePath; });
        return it != assets_.end() ? &(*it) : nullptr;
    }

    const std::vector<EditorAssetRecord>& EditorAssetDatabase::GetAssets() const noexcept
    {
        return assets_;
    }

    const char* EditorAssetDatabase::ToString(EditorAssetType type) noexcept
    {
        switch (type)
        {
        case EditorAssetType::ObjSource:
            return "OBJ";
        case EditorAssetType::Mesh:
            return "Mesh";
        case EditorAssetType::Material:
            return "Material";
        case EditorAssetType::Scene:
            return "Scene";
        case EditorAssetType::Unknown:
            break;
        }

        return "Unknown";
    }

    ErrorCode EditorAssetDatabase::ScanAndImportDirectory(const Path& physicalDirectoryPath, bool force)
    {
        Result<std::vector<FileSystem::DirectoryEntry>> entries = FileSystem::ListDirectory(physicalDirectoryPath);
        if (!entries)
        {
            return entries.GetError().GetCode();
        }

        for (const FileSystem::DirectoryEntry& entry : entries.GetValue())
        {
            if (entry.type == FileSystem::DirectoryEntryType::Directory)
            {
                const ErrorCode result = ScanAndImportDirectory(entry.path, force);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }
            else if (entry.type == FileSystem::DirectoryEntryType::File &&
                     GetAssetTypeFromExtension(entry.path) == EditorAssetType::ObjSource)
            {
                const ErrorCode result = ImportObjAsMesh(ToProjectRelativePath(entry.path), force);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }
        }

        return ErrorCode::None;
    }

    ErrorCode EditorAssetDatabase::ScanRecordsDirectory(const Path& physicalDirectoryPath)
    {
        Result<std::vector<FileSystem::DirectoryEntry>> entries = FileSystem::ListDirectory(physicalDirectoryPath);
        if (!entries)
        {
            return entries.GetError().GetCode();
        }

        for (const FileSystem::DirectoryEntry& entry : entries.GetValue())
        {
            if (entry.type == FileSystem::DirectoryEntryType::Directory)
            {
                const ErrorCode result = ScanRecordsDirectory(entry.path);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }
            else if (entry.type == FileSystem::DirectoryEntryType::File && IsSupportedAssetFile(entry.path))
            {
                EditorAssetRecord record;
                record.path = ToProjectRelativePath(entry.path);
                record.type = GetAssetTypeFromExtension(entry.path);
                record.imported = record.type == EditorAssetType::Mesh;

                if (record.type == EditorAssetType::Mesh)
                {
                    Result<Path> sourcePath = ReadMeshSourcePath(entry.path);
                    if (sourcePath)
                    {
                        record.sourcePath = sourcePath.GetValue();
                    }
                }

                AddAssetRecord(std::move(record));
            }
        }

        return ErrorCode::None;
    }

    ErrorCode EditorAssetDatabase::ImportObjAsMesh(const Path& objProjectPath, bool force)
    {
        if (objProjectPath.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const Path meshProjectPath = ReplaceExtension(objProjectPath, ".vemesh");
        const Path meshPhysicalPath = projectRoot_ / meshProjectPath;

        if (!force && FileSystem::IsFile(meshPhysicalPath))
        {
            return ErrorCode::None;
        }

        const boost::json::value meshJson(BuildMeshAssetJson(objProjectPath));
        return FileSystem::WriteTextFile(meshPhysicalPath, JsonUtils::SerializePretty(meshJson));
    }

    Result<Path> EditorAssetDatabase::ReadMeshSourcePath(const Path& meshPhysicalPath) const
    {
        Result<std::string> text = FileSystem::ReadTextFile(meshPhysicalPath);
        if (!text)
        {
            return Result<Path>::Failure(text.GetError());
        }

        Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
        if (!json)
        {
            return Result<Path>::Failure(json.GetError());
        }

        if (!json.GetValue().is_object())
        {
            return Result<Path>::Failure(
                Error(ErrorCode::InvalidArgument, "Mesh asset descriptor root must be a JSON object."));
        }

        const std::string sourceAsset = ReadString(json.GetValue().as_object(), "sourceAsset");
        return Result<Path>::Success(Path(sourceAsset));
    }

    Path EditorAssetDatabase::ToProjectRelativePath(const Path& physicalPath) const
    {
        const std::string& root = projectRoot_.GetString();
        const std::string& path = physicalPath.GetString();
        if (root.empty())
        {
            return physicalPath;
        }

        if (path == root)
        {
            return Path();
        }

        const std::string prefix = root.ends_with('/') ? root : root + "/";
        if (path.starts_with(prefix))
        {
            return Path(path.substr(prefix.size()));
        }

        return physicalPath;
    }

    void EditorAssetDatabase::AddAssetRecord(EditorAssetRecord record)
    {
        assets_.push_back(std::move(record));
    }
} // namespace ve::editor
