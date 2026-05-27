#include "Tools/Package/PackageService.h"

#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string_view>
#include <utility>

namespace ve
{
    namespace
    {
        using boost::json::array;
        using boost::json::object;
        using boost::json::value;

        constexpr std::string_view ProjectDescriptorFileName = ".veproject";
        constexpr std::string_view ManifestFileName = "AssetManifest.veassetmanifest";

        struct PackageProjectDescriptor
        {
            AssetGuid guid;
            AssetGuid startupSceneGuid;
            Path startupScenePath;
            std::string displayName;
            std::string engineVersion;
        };

        [[nodiscard]] Error MakeError(ErrorCode code, std::string message)
        {
            return Error(code, std::move(message));
        }

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

        [[nodiscard]] bool StartsWithPath(std::string_view text, std::string_view prefix) noexcept
        {
            return text == prefix || (text.size() > prefix.size() && text.starts_with(prefix) &&
                                      text[prefix.size()] == '/');
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
                    MakeError(ErrorCode::InvalidArgument, "JSON root must be an object: " + path.GetString()));
            }

            return Result<object>::Success(std::move(root.as_object()));
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

        [[nodiscard]] Result<AssetGuid> ReadGuid(const object& jsonObject, const char* name)
        {
            const value* member = FindMember(jsonObject, name);
            if (member == nullptr || !member->is_string())
            {
                return Result<AssetGuid>::Failure(MakeError(ErrorCode::InvalidArgument, "Missing GUID field."));
            }

            const boost::json::string& guidText = member->as_string();
            return AssetGuid::Parse(std::string_view(guidText.data(), guidText.size()));
        }

        [[nodiscard]] Result<PackageProjectDescriptor> LoadPackageProjectDescriptor(const Path& projectRoot)
        {
            const Path descriptorPath = projectRoot / ProjectDescriptorFileName;
            if (!FileSystem::IsFile(descriptorPath))
            {
                return Result<PackageProjectDescriptor>::Failure(
                    MakeError(ErrorCode::NotFound, "Project root does not contain .veproject."));
            }

            Result<object> jsonResult = ReadJsonObject(descriptorPath);
            if (!jsonResult)
            {
                return Result<PackageProjectDescriptor>::Failure(jsonResult.GetError());
            }

            const object& root = jsonResult.GetValue();
            if (ReadString(root, "format") != "VEngine.Project")
            {
                return Result<PackageProjectDescriptor>::Failure(
                    MakeError(ErrorCode::InvalidArgument, "Unsupported project descriptor format."));
            }

            PackageProjectDescriptor descriptor;
            Result<AssetGuid> guid = ReadGuid(root, "guid");
            if (!guid || !guid.GetValue().IsValid())
            {
                return Result<PackageProjectDescriptor>::Failure(
                    MakeError(ErrorCode::InvalidArgument, "Project descriptor GUID is invalid."));
            }

            descriptor.guid = guid.GetValue();
            descriptor.displayName = ReadString(root, "name");
            descriptor.engineVersion = ReadString(root, "engineVersion");

            const value* startupSceneValue = FindMember(root, "startupScene");
            if (startupSceneValue != nullptr && startupSceneValue->is_object())
            {
                const object& startupScene = startupSceneValue->as_object();
                if (Result<AssetGuid> sceneGuid = ReadGuid(startupScene, "guid"))
                {
                    descriptor.startupSceneGuid = sceneGuid.GetValue();
                }
                descriptor.startupScenePath = Path(ReadString(startupScene, "path"));
            }

            if (descriptor.displayName.empty() || descriptor.startupScenePath.IsEmpty())
            {
                return Result<PackageProjectDescriptor>::Failure(
                    MakeError(ErrorCode::InvalidArgument, "Project descriptor is missing name or startup scene."));
            }

            return Result<PackageProjectDescriptor>::Success(std::move(descriptor));
        }

        [[nodiscard]] Path MakeDefaultOutputRoot(const PackageRequest& request)
        {
            return request.projectRoot / "Generated/Build" / ToString(request.platform) /
                   ToString(request.configuration);
        }

        [[nodiscard]] Path MakePackageRoot(const PackageRequest& request)
        {
            const Path outputRoot = request.outputRoot.IsEmpty() ? MakeDefaultOutputRoot(request) : request.outputRoot;
            if (request.platform != PackagePlatform::iOS)
            {
                return outputRoot;
            }

            return outputRoot.GetExtension() == ".app" ? outputRoot : outputRoot / "VEngineIOSPlayer.app";
        }

        [[nodiscard]] Path MakeContentRoot(const Path& packageRoot)
        {
            return packageRoot / "Content";
        }

        [[nodiscard]] Path MakeRelativePath(const Path& root, const Path& path)
        {
            const std::string rootText = root.GetString();
            const std::string pathText = path.GetString();
            if (!StartsWithPath(pathText, rootText))
            {
                return path;
            }

            if (pathText.size() == rootText.size())
            {
                return {};
            }

            return Path(pathText.substr(rootText.size() + 1));
        }

        [[nodiscard]] ErrorCode CopyFileTracked(const Path& source,
                                                const Path& destination,
                                                PackageResult& result)
        {
            Result<std::vector<std::byte>> data = FileSystem::ReadBinaryFile(source);
            if (!data)
            {
                return data.GetError().GetCode();
            }

            const ErrorCode writeResult = FileSystem::WriteBinaryFile(destination, data.GetValue());
            if (writeResult == ErrorCode::None)
            {
                result.stagedFiles.push_back(destination);
            }

            return writeResult;
        }

        [[nodiscard]] ErrorCode CopyProjectRelativeFile(const Path& projectRoot,
                                                        const Path& contentRoot,
                                                        const Path& relativePath,
                                                        PackageResult& result)
        {
            if (relativePath.IsEmpty())
            {
                return ErrorCode::None;
            }

            return CopyFileTracked(projectRoot / relativePath, contentRoot / relativePath, result);
        }

        [[nodiscard]] ErrorCode CopyDirectoryRecursive(const Path& sourceRoot,
                                                       const Path& destinationRoot,
                                                       PackageResult& result)
        {
            if (!FileSystem::Exists(sourceRoot))
            {
                return ErrorCode::None;
            }

            Result<std::vector<FileSystem::DirectoryEntry>> entries = FileSystem::ListDirectory(sourceRoot);
            if (!entries)
            {
                return entries.GetError().GetCode();
            }

            for (const FileSystem::DirectoryEntry& entry : entries.GetValue())
            {
                const Path relativePath = MakeRelativePath(sourceRoot, entry.path);
                const Path destination = destinationRoot / relativePath;
                if (entry.type == FileSystem::DirectoryEntryType::Directory)
                {
                    const ErrorCode directoryResult = FileSystem::CreateDirectories(destination);
                    if (directoryResult != ErrorCode::None)
                    {
                        return directoryResult;
                    }

                    const ErrorCode copyResult = CopyDirectoryRecursive(entry.path, destination, result);
                    if (copyResult != ErrorCode::None)
                    {
                        return copyResult;
                    }
                }
                else if (entry.type == FileSystem::DirectoryEntryType::File)
                {
                    const ErrorCode copyResult = CopyFileTracked(entry.path, destination, result);
                    if (copyResult != ErrorCode::None)
                    {
                        return copyResult;
                    }
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] bool IsAuthoredNativeAsset(AssetType type) noexcept
        {
            return type == AssetType::Scene || type == AssetType::Material;
        }

        [[nodiscard]] ErrorCode ValidatePackageAssets(const AssetDatabase& assetDatabase)
        {
            const ErrorCode databaseResult = assetDatabase.Validate();
            if (databaseResult != ErrorCode::None)
            {
                return databaseResult;
            }

            for (const AssetRecord& record : assetDatabase.GetRecords())
            {
                if (record.assetType == AssetType::SourceModel &&
                    (!record.guid.IsValid() || record.metadataPath.IsEmpty() || record.artifacts.empty()))
                {
                    return ErrorCode::InvalidState;
                }

                if (IsAuthoredNativeAsset(record.assetType) && (!record.guid.IsValid() || record.path.IsEmpty()))
                {
                    return ErrorCode::InvalidState;
                }
            }

            return ErrorCode::None;
        }

        [[nodiscard]] object WriteArtifactJson(const AssetArtifact& artifact)
        {
            object artifactJson;
            artifactJson["type"] = artifact.type;
            artifactJson["path"] = artifact.path.GetString();
            return artifactJson;
        }

        [[nodiscard]] object WriteDependencyJson(const AssetDependency& dependency)
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

        [[nodiscard]] object WriteAssetRecordJson(const AssetRecord& record)
        {
            object assetJson;
            if (record.guid.IsValid())
            {
                assetJson["guid"] = record.guid.ToString();
            }
            assetJson["type"] = ToString(record.assetType);
            assetJson["path"] = record.path.GetString();
            if (!record.source.IsEmpty())
            {
                assetJson["source"] = record.source.GetString();
            }
            if (!record.metadataPath.IsEmpty())
            {
                assetJson["metadata"] = record.metadataPath.GetString();
            }

            array artifacts;
            for (const AssetArtifact& artifact : record.artifacts)
            {
                artifacts.push_back(WriteArtifactJson(artifact));
            }

            array dependencies;
            for (const AssetDependency& dependency : record.dependencies)
            {
                dependencies.push_back(WriteDependencyJson(dependency));
            }

            assetJson["artifacts"] = std::move(artifacts);
            assetJson["dependencies"] = std::move(dependencies);
            return assetJson;
        }

        [[nodiscard]] object WriteManifestJson(const PackageRequest& request,
                                               const PackageProjectDescriptor& descriptor,
                                               const AssetDatabase& assetDatabase,
                                               const PackageResult& result)
        {
            object projectJson;
            projectJson["guid"] = descriptor.guid.ToString();
            projectJson["name"] = descriptor.displayName;
            projectJson["engineVersion"] =
                descriptor.engineVersion.empty() ? std::string(GetBuildInfo().version) : descriptor.engineVersion;

            object startupSceneJson;
            if (descriptor.startupSceneGuid.IsValid())
            {
                startupSceneJson["guid"] = descriptor.startupSceneGuid.ToString();
            }
            startupSceneJson["path"] = descriptor.startupScenePath.GetString();
            projectJson["startupScene"] = std::move(startupSceneJson);

            array assets;
            for (const AssetRecord& record : assetDatabase.GetRecords())
            {
                assets.push_back(WriteAssetRecordJson(record));
            }

            object root;
            root["format"] = "VEngine.AssetManifest";
            root["version"] = 1;
            root["platform"] = ToString(request.platform);
            root["configuration"] = ToString(request.configuration);
            root["contentRoot"] = "Content";
            root["readOnlyContent"] = true;
            root["project"] = std::move(projectJson);
            root["assetCount"] = static_cast<std::uint64_t>(assetDatabase.GetRecords().size());
            root["artifactCount"] = static_cast<std::uint64_t>(result.artifactCount);
            root["assets"] = std::move(assets);

            array shaderRoots;
            if (request.platform == PackagePlatform::Windows)
            {
                shaderRoots.push_back("Generated/Shaders/Windows/D3D11");
                shaderRoots.push_back("Generated/Shaders/Windows/D3D12");
            }
            else if (request.platform == PackagePlatform::iOS)
            {
                shaderRoots.push_back("Generated/Shaders/iOS/Metal");
            }
            root["shaderRoots"] = std::move(shaderRoots);
            return root;
        }

        [[nodiscard]] ErrorCode CopyPackageContent(const PackageRequest& request,
                                                   const AssetDatabase& assetDatabase,
                                                   PackageResult& result)
        {
            const ErrorCode descriptorResult =
                CopyFileTracked(request.projectRoot / ProjectDescriptorFileName,
                                result.contentRoot / ProjectDescriptorFileName,
                                result);
            if (descriptorResult != ErrorCode::None)
            {
                return descriptorResult;
            }

            for (const AssetRecord& record : assetDatabase.GetRecords())
            {
                if (IsAuthoredNativeAsset(record.assetType))
                {
                    const ErrorCode copyResult =
                        CopyProjectRelativeFile(request.projectRoot, result.contentRoot, record.path, result);
                    if (copyResult != ErrorCode::None)
                    {
                        return copyResult;
                    }
                }

                if (record.assetType == AssetType::SourceModel && !record.metadataPath.IsEmpty())
                {
                    const ErrorCode copyResult =
                        CopyProjectRelativeFile(request.projectRoot, result.contentRoot, record.metadataPath, result);
                    if (copyResult != ErrorCode::None)
                    {
                        return copyResult;
                    }
                }

                for (const AssetArtifact& artifact : record.artifacts)
                {
                    const ErrorCode copyResult =
                        CopyProjectRelativeFile(request.projectRoot, result.contentRoot, artifact.path, result);
                    if (copyResult != ErrorCode::None)
                    {
                        return copyResult;
                    }

                    ++result.artifactCount;
                }
            }

            const Path destinationShaders = result.contentRoot / "Generated/Shaders";
            if (request.platform == PackagePlatform::Windows)
            {
                const ErrorCode shaderCopyResult = CopyDirectoryRecursive(request.projectRoot /
                                                                              "Generated/Shaders/Windows",
                                                                          destinationShaders / "Windows",
                                                                          result);
                if (shaderCopyResult != ErrorCode::None)
                {
                    return shaderCopyResult;
                }

                ErrorCode d3d11Result = FileSystem::CreateDirectories(destinationShaders / "Windows/D3D11");
                if (d3d11Result != ErrorCode::None)
                {
                    return d3d11Result;
                }

                return FileSystem::CreateDirectories(destinationShaders / "Windows/D3D12");
            }

            const ErrorCode shaderCopyResult = CopyDirectoryRecursive(request.projectRoot / "Generated/Shaders/iOS",
                                                                      destinationShaders / "iOS",
                                                                      result);
            if (shaderCopyResult != ErrorCode::None)
            {
                return shaderCopyResult;
            }

            return FileSystem::CreateDirectories(destinationShaders / "iOS/Metal");
        }

        [[nodiscard]] Path FindDefaultPlayerExecutable()
        {
            return FileSystem::GetExecutableDirectory() / "VEnginePlayer.exe";
        }

        [[nodiscard]] bool IsDllPath(const Path& path)
        {
            return EqualsIgnoreCase(path.GetExtension(), ".dll");
        }

        [[nodiscard]] ErrorCode CopyWindowsRuntimeBinaries(const PackageRequest& request, PackageResult& result)
        {
            if (request.platform != PackagePlatform::Windows || !request.includeRuntimeBinaries)
            {
                return ErrorCode::None;
            }

            const Path playerExecutable =
                request.playerExecutable.IsEmpty() ? FindDefaultPlayerExecutable() : request.playerExecutable;
            if (!FileSystem::IsFile(playerExecutable))
            {
                return ErrorCode::NotFound;
            }

            const ErrorCode playerCopyResult =
                CopyFileTracked(playerExecutable, result.packageRoot / "VEnginePlayer.exe", result);
            if (playerCopyResult != ErrorCode::None)
            {
                return playerCopyResult;
            }

            const Path playerDirectory = playerExecutable.GetParentPath();
            Result<std::vector<FileSystem::DirectoryEntry>> entries = FileSystem::ListDirectory(playerDirectory);
            if (!entries)
            {
                return entries.GetError().GetCode();
            }

            for (const FileSystem::DirectoryEntry& entry : entries.GetValue())
            {
                if (entry.type == FileSystem::DirectoryEntryType::File && IsDllPath(entry.path))
                {
                    const ErrorCode copyResult =
                        CopyFileTracked(entry.path, result.packageRoot / entry.path.GetFilename(), result);
                    if (copyResult != ErrorCode::None)
                    {
                        return copyResult;
                    }
                }
            }

            return ErrorCode::None;
        }
    } // namespace

    const char* ToString(PackagePlatform platform) noexcept
    {
        switch (platform)
        {
        case PackagePlatform::Windows:
            return "Windows";
        case PackagePlatform::iOS:
            return "iOS";
        default:
            return "Unknown";
        }
    }

    const char* ToString(PackageConfiguration configuration) noexcept
    {
        switch (configuration)
        {
        case PackageConfiguration::Debug:
            return "Debug";
        case PackageConfiguration::Release:
            return "Release";
        default:
            return "Unknown";
        }
    }

    Result<PackagePlatform> ParsePackagePlatform(std::string_view text)
    {
        if (EqualsIgnoreCase(text, "Windows"))
        {
            return Result<PackagePlatform>::Success(PackagePlatform::Windows);
        }

        if (EqualsIgnoreCase(text, "iOS"))
        {
            return Result<PackagePlatform>::Success(PackagePlatform::iOS);
        }

        return Result<PackagePlatform>::Failure(MakeError(ErrorCode::InvalidArgument, "Unsupported package platform."));
    }

    Result<PackageConfiguration> ParsePackageConfiguration(std::string_view text)
    {
        if (EqualsIgnoreCase(text, "Debug"))
        {
            return Result<PackageConfiguration>::Success(PackageConfiguration::Debug);
        }

        if (EqualsIgnoreCase(text, "Release"))
        {
            return Result<PackageConfiguration>::Success(PackageConfiguration::Release);
        }

        return Result<PackageConfiguration>::Failure(
            MakeError(ErrorCode::InvalidArgument, "Unsupported package configuration."));
    }

    Result<PackageResult> StagePackage(const PackageRequest& request)
    {
        if (request.projectRoot.IsEmpty() || !FileSystem::IsDirectory(request.projectRoot))
        {
            return Result<PackageResult>::Failure(MakeError(ErrorCode::InvalidArgument, "Invalid project root."));
        }

        Result<PackageProjectDescriptor> descriptor = LoadPackageProjectDescriptor(request.projectRoot);
        if (!descriptor)
        {
            return Result<PackageResult>::Failure(descriptor.GetError());
        }

        AssetDatabase assetDatabase;
        const ErrorCode openResult = assetDatabase.Open(request.projectRoot);
        if (openResult != ErrorCode::None)
        {
            return Result<PackageResult>::Failure(MakeError(openResult, "Failed to open AssetDatabase."));
        }

        const ErrorCode validateResult = ValidatePackageAssets(assetDatabase);
        if (validateResult != ErrorCode::None)
        {
            return Result<PackageResult>::Failure(
                MakeError(validateResult, "Project assets are not ready for packaging."));
        }

        PackageResult result;
        result.packageRoot = MakePackageRoot(request);
        result.contentRoot = MakeContentRoot(result.packageRoot);
        result.manifestPath = result.contentRoot / ManifestFileName;
        result.assetCount = assetDatabase.GetRecords().size();

        const ErrorCode contentDirectoryResult = FileSystem::CreateDirectories(result.contentRoot);
        if (contentDirectoryResult != ErrorCode::None)
        {
            return Result<PackageResult>::Failure(
                MakeError(contentDirectoryResult, "Failed to create package Content directory."));
        }

        const ErrorCode contentCopyResult = CopyPackageContent(request, assetDatabase, result);
        if (contentCopyResult != ErrorCode::None)
        {
            return Result<PackageResult>::Failure(MakeError(contentCopyResult, "Failed to stage package content."));
        }

        const object manifest = WriteManifestJson(request, descriptor.GetValue(), assetDatabase, result);
        const std::string manifestText = boost::json::serialize(manifest);
        const ErrorCode manifestResult = FileSystem::WriteTextFile(result.manifestPath, manifestText);
        if (manifestResult != ErrorCode::None)
        {
            return Result<PackageResult>::Failure(MakeError(manifestResult, "Failed to write asset manifest."));
        }
        result.stagedFiles.push_back(result.manifestPath);

        const ErrorCode runtimeCopyResult = CopyWindowsRuntimeBinaries(request, result);
        if (runtimeCopyResult != ErrorCode::None)
        {
            return Result<PackageResult>::Failure(
                MakeError(runtimeCopyResult, "Failed to stage runtime binaries."));
        }

        return Result<PackageResult>::Success(std::move(result));
    }
} // namespace ve
