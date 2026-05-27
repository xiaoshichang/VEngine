#include "Engine/Runtime/Asset/AssetDatabase.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <algorithm>
#include <cctype>
#include <utility>

namespace ve
{
    namespace
    {
        using boost::json::array;
        using boost::json::object;
        using boost::json::value;

        [[nodiscard]] bool EqualsIgnoreCase(std::string_view left, std::string_view right) noexcept
        {
            if (left.size() != right.size())
            {
                return false;
            }

            for (SizeT index = 0; index < left.size(); ++index)
            {
                const char leftChar = static_cast<char>(std::tolower(static_cast<unsigned char>(left[index])));
                const char rightChar = static_cast<char>(std::tolower(static_cast<unsigned char>(right[index])));
                if (leftChar != rightChar)
                {
                    return false;
                }
            }

            return true;
        }

        [[nodiscard]] bool StartsWithIgnoreCase(std::string_view text, std::string_view prefix) noexcept
        {
            return text.size() >= prefix.size() && EqualsIgnoreCase(text.substr(0, prefix.size()), prefix);
        }

        [[nodiscard]] bool IsObjSourceAsset(const Path& path)
        {
            return EqualsIgnoreCase(path.GetExtension(), ".obj");
        }

        [[nodiscard]] const value* FindMember(const object& jsonObject, const char* name)
        {
            const auto iter = jsonObject.find(name);
            return iter == jsonObject.end() ? nullptr : &iter->value();
        }

        [[nodiscard]] std::string ReadString(const object& jsonObject, const char* name, std::string fallback = {})
        {
            const value* member = FindMember(jsonObject, name);
            return member != nullptr && member->is_string() ? std::string(member->as_string()) : std::move(fallback);
        }

        [[nodiscard]] UInt32 ReadUInt32(const object& jsonObject, const char* name, UInt32 fallback) noexcept
        {
            const value* member = FindMember(jsonObject, name);
            if (member == nullptr)
            {
                return fallback;
            }

            if (member->is_uint64())
            {
                return static_cast<UInt32>(member->as_uint64());
            }

            if (member->is_int64() && member->as_int64() >= 0)
            {
                return static_cast<UInt32>(member->as_int64());
            }

            return fallback;
        }

        [[nodiscard]] Result<object> ReadJsonObject(const Path& path)
        {
            Result<std::string> textResult = FileSystem::ReadTextFile(path);
            if (!textResult)
            {
                return Result<object>::Failure(textResult.GetError());
            }

            boost::system::error_code parseError;
            value root = boost::json::parse(textResult.GetValue(), parseError);
            if (parseError || !root.is_object())
            {
                return Result<object>::Failure(
                    Error(ErrorCode::InvalidArgument, "Asset JSON root must be an object: " + path.GetString()));
            }

            return Result<object>::Success(std::move(root.as_object()));
        }

        [[nodiscard]] Result<AssetGuid> ReadGuid(const object& jsonObject, const char* name)
        {
            const value* member = FindMember(jsonObject, name);
            if (member == nullptr || !member->is_string())
            {
                return Result<AssetGuid>::Failure(Error(ErrorCode::InvalidArgument, "Missing asset GUID field."));
            }

            const boost::json::string& guidText = member->as_string();
            return AssetGuid::Parse(std::string_view(guidText.data(), guidText.size()));
        }

        [[nodiscard]] std::vector<AssetArtifact> ReadArtifacts(const object& jsonObject)
        {
            std::vector<AssetArtifact> artifacts;
            const value* artifactsValue = FindMember(jsonObject, "artifacts");
            if (artifactsValue == nullptr || !artifactsValue->is_array())
            {
                return artifacts;
            }

            for (const value& artifactValue : artifactsValue->as_array())
            {
                if (!artifactValue.is_object())
                {
                    continue;
                }

                const object& artifactJson = artifactValue.as_object();
                AssetArtifact artifact;
                artifact.type = ReadString(artifactJson, "type");
                artifact.path = Path(ReadString(artifactJson, "path"));

                if (!artifact.type.empty() && !artifact.path.IsEmpty())
                {
                    artifacts.push_back(std::move(artifact));
                }
            }

            return artifacts;
        }

        [[nodiscard]] std::vector<AssetDependency> ReadDependencies(const object& jsonObject)
        {
            std::vector<AssetDependency> dependencies;
            const value* dependenciesValue = FindMember(jsonObject, "dependencies");
            if (dependenciesValue == nullptr || !dependenciesValue->is_array())
            {
                return dependencies;
            }

            for (const value& dependencyValue : dependenciesValue->as_array())
            {
                if (!dependencyValue.is_object())
                {
                    continue;
                }

                const object& dependencyJson = dependencyValue.as_object();
                AssetDependency dependency;
                dependency.type = ReadString(dependencyJson, "type");
                dependency.path = Path(ReadString(dependencyJson, "path"));

                if (Result<AssetGuid> guid = ReadGuid(dependencyJson, "guid"))
                {
                    dependency.guid = guid.GetValue();
                }

                dependencies.push_back(std::move(dependency));
            }

            return dependencies;
        }

        [[nodiscard]] object WriteArtifact(const AssetArtifact& artifact)
        {
            object artifactJson;
            artifactJson["type"] = artifact.type;
            artifactJson["path"] = artifact.path.GetString();
            return artifactJson;
        }

        [[nodiscard]] object WriteDependency(const AssetDependency& dependency)
        {
            object dependencyJson;
            dependencyJson["type"] = dependency.type;
            dependencyJson["path"] = dependency.path.GetString();
            if (dependency.guid.IsValid())
            {
                dependencyJson["guid"] = dependency.guid.ToString();
            }

            return dependencyJson;
        }
    } // namespace

    const char* ToString(AssetType type) noexcept
    {
        switch (type)
        {
        case AssetType::SourceModel:
            return "SourceModel";
        case AssetType::Scene:
            return "Scene";
        case AssetType::Material:
            return "Material";
        case AssetType::Mesh:
            return "Mesh";
        case AssetType::Unknown:
        default:
            return "Unknown";
        }
    }

    AssetType ParseAssetType(std::string_view text) noexcept
    {
        if (text == "SourceModel")
        {
            return AssetType::SourceModel;
        }

        if (text == "Scene")
        {
            return AssetType::Scene;
        }

        if (text == "Material")
        {
            return AssetType::Material;
        }

        if (text == "Mesh")
        {
            return AssetType::Mesh;
        }

        return AssetType::Unknown;
    }

    ErrorCode AssetDatabase::Open(Path projectRoot)
    {
        if (projectRoot.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        projectRoot_ = std::move(projectRoot);
        return Refresh();
    }

    ErrorCode AssetDatabase::Refresh()
    {
        records_.clear();
        diagnostics_.clear();

        if (projectRoot_.IsEmpty())
        {
            return ErrorCode::InvalidState;
        }

        const Path assetsRoot = projectRoot_ / "Assets";
        if (!FileSystem::Exists(assetsRoot))
        {
            return ErrorCode::None;
        }

        return ScanDirectory(assetsRoot);
    }

    ErrorCode AssetDatabase::Validate() const
    {
        bool valid = true;

        for (const AssetRecord& record : records_)
        {
            if (record.assetType == AssetType::SourceModel)
            {
                if (record.source.IsEmpty() || !FileSystem::IsFile(ResolveProjectPath(record.source)))
                {
                    VE_LOG_ERROR_CATEGORY("Asset", "Missing source asset: {}", record.source.GetString());
                    valid = false;
                }

                for (const AssetArtifact& artifact : record.artifacts)
                {
                    if (!FileSystem::IsFile(ResolveProjectPath(artifact.path)))
                    {
                        VE_LOG_ERROR_CATEGORY("Asset", "Missing generated artifact: {}", artifact.path.GetString());
                        valid = false;
                    }
                }
            }
        }

        return valid ? ErrorCode::None : ErrorCode::NotFound;
    }

    const Path& AssetDatabase::GetProjectRoot() const noexcept
    {
        return projectRoot_;
    }

    const std::vector<AssetRecord>& AssetDatabase::GetRecords() const noexcept
    {
        return records_;
    }

    const AssetRecord* AssetDatabase::FindAsset(const AssetGuid& guid) const noexcept
    {
        if (!guid.IsValid())
        {
            return nullptr;
        }

        const std::string guidText = guid.ToString();
        const auto iter = std::find_if(records_.begin(),
                                       records_.end(),
                                       [&guidText](const AssetRecord& record)
                                       { return record.guid.ToString() == guidText; });
        return iter == records_.end() ? nullptr : &(*iter);
    }

    const AssetRecord* AssetDatabase::FindAssetByPath(const Path& projectRelativePath) const noexcept
    {
        const auto iter = std::find_if(records_.begin(),
                                       records_.end(),
                                       [&projectRelativePath](const AssetRecord& record)
                                       {
                                           return record.path == projectRelativePath ||
                                                  record.source == projectRelativePath ||
                                                  record.metadataPath == projectRelativePath;
                                       });
        return iter == records_.end() ? nullptr : &(*iter);
    }

    Result<Path> AssetDatabase::ResolveArtifact(const AssetGuid& guid, std::string_view artifactType) const
    {
        const AssetRecord* record = FindAsset(guid);
        if (record == nullptr)
        {
            return Result<Path>::Failure(Error(ErrorCode::NotFound, "Asset GUID was not found."));
        }

        for (const AssetArtifact& artifact : record->artifacts)
        {
            if (artifact.type == artifactType)
            {
                return Result<Path>::Success(artifact.path);
            }
        }

        return Result<Path>::Failure(Error(ErrorCode::NotFound, "Asset artifact was not found."));
    }

    Path AssetDatabase::MakeProjectRelativePath(const Path& path) const
    {
        if (path.IsEmpty())
        {
            return {};
        }

        if (!path.IsAbsolute())
        {
            return path;
        }

        std::string rootText = projectRoot_.GetString();
        if (!rootText.ends_with('/'))
        {
            rootText.push_back('/');
        }

        const std::string& pathText = path.GetString();
        if (StartsWithIgnoreCase(pathText, rootText))
        {
            return Path(pathText.substr(rootText.size()));
        }

        return path;
    }

    Path AssetDatabase::ResolveProjectPath(const Path& projectRelativePath) const
    {
        return projectRelativePath.IsAbsolute() ? projectRelativePath : projectRoot_ / projectRelativePath;
    }

    Result<SourceAssetMetadata> AssetDatabase::LoadSourceMetadata(const Path& metadataPath) const
    {
        const Path absolutePath = ResolveProjectPath(metadataPath);
        Result<object> jsonResult = ReadJsonObject(absolutePath);
        if (!jsonResult)
        {
            return Result<SourceAssetMetadata>::Failure(jsonResult.GetError());
        }

        const object& root = jsonResult.GetValue();
        if (ReadString(root, "format") != "VEngine.AssetMetadata")
        {
            return Result<SourceAssetMetadata>::Failure(
                Error(ErrorCode::InvalidArgument, "Unsupported source asset metadata format."));
        }

        SourceAssetMetadata metadata;

        Result<AssetGuid> guid = ReadGuid(root, "guid");
        if (!guid)
        {
            return Result<SourceAssetMetadata>::Failure(guid.GetError());
        }

        metadata.guid = guid.GetValue();
        metadata.assetType = ParseAssetType(ReadString(root, "assetType"));
        metadata.source = Path(ReadString(root, "source"));
        metadata.sourceHash = ReadString(root, "sourceHash");
        metadata.importer = ReadString(root, "importer");
        metadata.importerVersion = ReadUInt32(root, "importerVersion", 1);
        metadata.artifacts = ReadArtifacts(root);
        metadata.dependencies = ReadDependencies(root);

        return Result<SourceAssetMetadata>::Success(std::move(metadata));
    }

    ErrorCode AssetDatabase::SaveSourceMetadata(const SourceAssetMetadata& metadata) const
    {
        if (!metadata.guid.IsValid() || metadata.source.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        object root;
        root["format"] = "VEngine.AssetMetadata";
        root["version"] = 1;
        root["guid"] = metadata.guid.ToString();
        root["assetType"] = ToString(metadata.assetType);
        root["source"] = metadata.source.GetString();
        root["sourceHash"] = metadata.sourceHash;
        root["importer"] = metadata.importer;
        root["importerVersion"] = metadata.importerVersion;
        root["settings"] = object();

        array artifacts;
        for (const AssetArtifact& artifact : metadata.artifacts)
        {
            artifacts.push_back(WriteArtifact(artifact));
        }

        array dependencies;
        for (const AssetDependency& dependency : metadata.dependencies)
        {
            dependencies.push_back(WriteDependency(dependency));
        }

        root["artifacts"] = std::move(artifacts);
        root["dependencies"] = std::move(dependencies);

        return FileSystem::WriteTextFile(ResolveProjectPath(GetSourceMetadataPath(metadata.source)),
                                         boost::json::serialize(root));
    }

    Path AssetDatabase::GetSourceMetadataPath(const Path& sourcePath) const
    {
        const Path relativeSource = MakeProjectRelativePath(sourcePath);
        return Path(relativeSource.GetString() + ".veasset");
    }

    ErrorCode AssetDatabase::ScanDirectory(const Path& path)
    {
        Result<std::vector<FileSystem::DirectoryEntry>> entries = FileSystem::ListDirectory(path);
        if (!entries)
        {
            return entries.GetError().GetCode();
        }

        for (const FileSystem::DirectoryEntry& entry : entries.GetValue())
        {
            if (entry.type == FileSystem::DirectoryEntryType::Directory)
            {
                const ErrorCode result = ScanDirectory(entry.path);
                if (result != ErrorCode::None)
                {
                    return result;
                }

                continue;
            }

            if (entry.type != FileSystem::DirectoryEntryType::File)
            {
                continue;
            }

            if (entry.path.GetString().ends_with(".veasset"))
            {
                Result<SourceAssetMetadata> metadata = LoadSourceMetadata(MakeProjectRelativePath(entry.path));
                if (!metadata)
                {
                    VE_LOG_WARN_CATEGORY("Asset", "Skipping invalid asset metadata: {}", entry.path.GetString());
                    continue;
                }

                AssetRecord record;
                record.guid = metadata.GetValue().guid;
                record.assetType = metadata.GetValue().assetType;
                record.metadataPath = MakeProjectRelativePath(entry.path);
                record.path = record.metadataPath;
                record.source = metadata.GetValue().source;
                record.artifacts = metadata.GetValue().artifacts;
                record.dependencies = metadata.GetValue().dependencies;

                const ErrorCode result = AddRecord(std::move(record));
                if (result != ErrorCode::None)
                {
                    return result;
                }

                continue;
            }

            if (IsObjSourceAsset(entry.path))
            {
                const ErrorCode result = AddSourceAssetCandidateRecord(entry.path);
                if (result != ErrorCode::None)
                {
                    return result;
                }

                continue;
            }

            if (entry.path.GetExtension() == ".vescene" || entry.path.GetExtension() == ".vematerial")
            {
                const ErrorCode result = AddNativeAssetRecord(entry.path);
                if (result != ErrorCode::None)
                {
                    return result;
                }
            }
        }

        return ErrorCode::None;
    }

    ErrorCode AssetDatabase::AddRecord(AssetRecord record)
    {
        if (!record.guid.IsValid())
        {
            return ErrorCode::InvalidArgument;
        }

        if (FindAsset(record.guid) != nullptr)
        {
            VE_LOG_ERROR_CATEGORY("Asset", "Duplicate asset GUID: {}", record.guid.ToString());
            return ErrorCode::AlreadyExists;
        }

        records_.push_back(std::move(record));
        return ErrorCode::None;
    }

    ErrorCode AssetDatabase::AddSourceAssetCandidateRecord(const Path& path)
    {
        const Path relativePath = MakeProjectRelativePath(path);
        const Path metadataPath = GetSourceMetadataPath(relativePath);
        if (FileSystem::IsFile(ResolveProjectPath(metadataPath)))
        {
            return ErrorCode::None;
        }

        AssetRecord record;
        record.assetType = AssetType::SourceModel;
        record.path = relativePath;
        record.metadataPath = metadataPath;
        record.source = relativePath;
        records_.push_back(std::move(record));
        return ErrorCode::None;
    }

    ErrorCode AssetDatabase::AddNativeAssetRecord(const Path& path)
    {
        Result<object> jsonResult = ReadJsonObject(path);
        if (!jsonResult)
        {
            VE_LOG_WARN_CATEGORY("Asset", "Skipping invalid native asset: {}", path.GetString());
            return ErrorCode::None;
        }

        const object& root = jsonResult.GetValue();
        const std::string format = ReadString(root, "format");

        AssetType assetType = AssetType::Unknown;
        if (format == "VEngine.Scene")
        {
            assetType = AssetType::Scene;
        }
        else if (format == "VEngine.Material")
        {
            assetType = AssetType::Material;
        }
        else
        {
            return ErrorCode::None;
        }

        Result<AssetGuid> guid = ReadGuid(root, "guid");
        if (!guid)
        {
            VE_LOG_WARN_CATEGORY("Asset", "Skipping native asset without GUID: {}", path.GetString());
            return ErrorCode::None;
        }

        AssetRecord record;
        record.guid = guid.GetValue();
        record.assetType = assetType;
        record.path = MakeProjectRelativePath(path);
        return AddRecord(std::move(record));
    }
} // namespace ve
