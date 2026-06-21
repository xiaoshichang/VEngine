#include "Editor/Core/EditorAssetDatabase.h"

#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Resource/MaterialProperty.h"

#include <algorithm>
#include <array>
#include <boost/json.hpp>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace ve::editor
{
    namespace
    {
        constexpr const char* ImportedDirectoryName = "Imported";

        struct ObjMeshData
        {
            std::string name;
            std::vector<std::array<double, 3>> vertices;
            std::vector<std::array<double, 3>> normals;
            std::vector<UInt32> indices;
            std::array<double, 3> boundsCenter = {0.0, 0.0, 0.0};
            std::array<double, 3> boundsExtents = {0.0, 0.0, 0.0};
        };

        [[nodiscard]] std::string ToLowerCopy(std::string text)
        {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
            return text;
        }

        [[nodiscard]] EditorAssetType GetAssetTypeFromExtension(const Path& path)
        {
            const std::string extension = ToLowerCopy(path.GetExtension());
            if (extension == ".obj")
            {
                return EditorAssetType::ObjSource;
            }

            if (extension == ".vematerial")
            {
                return EditorAssetType::Material;
            }

            if (extension == ".veshader")
            {
                return EditorAssetType::Shader;
            }

            if (extension == ".vescene")
            {
                return EditorAssetType::Scene;
            }

            return EditorAssetType::Unknown;
        }

        [[nodiscard]] bool IsMetaFile(const Path& path)
        {
            return ToLowerCopy(path.GetString()).ends_with(".meta");
        }

        [[nodiscard]] bool IsSupportedNativeAssetFile(const Path& path)
        {
            return GetAssetTypeFromExtension(path) != EditorAssetType::Unknown && !IsMetaFile(path);
        }

        [[nodiscard]] const char* ToMetaAssetType(EditorAssetType type) noexcept
        {
            switch (type)
            {
            case EditorAssetType::ObjSource:
                return "ObjSource";
            case EditorAssetType::Mesh:
                return "Mesh";
            case EditorAssetType::Material:
                return "Material";
            case EditorAssetType::Shader:
                return "Shader";
            case EditorAssetType::Scene:
                return "Scene";
            case EditorAssetType::Unknown:
                break;
            }

            return "Unknown";
        }

        [[nodiscard]] const char* GetDefaultImporter(EditorAssetType type) noexcept
        {
            if (type == EditorAssetType::ObjSource)
            {
                return "ObjMeshImporter";
            }

            if (type == EditorAssetType::Shader)
            {
                return "ShaderImporter";
            }

            return "";
        }

        [[nodiscard]] ResourceType ToResourceType(EditorAssetType type) noexcept
        {
            switch (type)
            {
            case EditorAssetType::ObjSource:
            case EditorAssetType::Mesh:
                return ResourceType::Mesh;
            case EditorAssetType::Material:
                return ResourceType::Material;
            case EditorAssetType::Shader:
                return ResourceType::Shader;
            case EditorAssetType::Scene:
                return ResourceType::Scene;
            case EditorAssetType::Unknown:
                break;
            }

            return ResourceType::Unknown;
        }

        [[nodiscard]] std::string ReadString(const boost::json::object& object, boost::json::string_view key, std::string fallback = {})
        {
            if (const boost::json::value* value = object.if_contains(key); value != nullptr && value->is_string())
            {
                return std::string(value->as_string());
            }

            return fallback;
        }

        [[nodiscard]] std::string GetStem(const Path& path)
        {
            const std::string filename = path.GetFilename();
            const std::string extension = path.GetExtension();
            if (extension.empty() || extension.size() >= filename.size())
            {
                return filename;
            }

            return filename.substr(0, filename.size() - extension.size());
        }

        [[nodiscard]] std::string GetImportedMeshFilename(const Path& objProjectPath)
        {
            return GetStem(objProjectPath) + ".vemesh";
        }

        [[nodiscard]] std::string GetImportedShaderFilename(std::string_view shaderName)
        {
            return std::string(shaderName) + ".veshader.json";
        }

        [[nodiscard]] std::string QuoteProcessArgument(std::string_view argument)
        {
            std::string quoted = "\"";
            for (const char value : argument)
            {
                if (value == '"')
                {
                    quoted += "\\\"";
                }
                else
                {
                    quoted += value;
                }
            }

            quoted += "\"";
            return quoted;
        }

#if defined(_WIN32)
        [[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredLength = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (requiredLength <= 0)
            {
                return {};
            }

            std::wstring wideText(static_cast<size_t>(requiredLength), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), wideText.data(), requiredLength);
            return wideText;
        }
#endif

        [[nodiscard]] std::string BuildProcessCommandLine(const std::vector<std::string>& arguments)
        {
            std::ostringstream command;
            for (SizeT index = 0; index < arguments.size(); ++index)
            {
                if (index > 0)
                {
                    command << ' ';
                }

                command << QuoteProcessArgument(arguments[index]);
            }

            return command.str();
        }

        [[nodiscard]] int RunProcess(const std::vector<std::string>& arguments)
        {
            if (arguments.empty())
            {
                return -1;
            }

            const std::string commandLineText = BuildProcessCommandLine(arguments);
#if defined(_WIN32)
            std::wstring commandLine = Utf8ToWide(commandLineText);
            std::wstring applicationPath = Utf8ToWide(arguments[0]);
            std::wstring workingDirectory = Utf8ToWide(FileSystem::GetCurrentWorkingDirectory().GetString());
            if (commandLine.empty() || applicationPath.empty())
            {
                return -1;
            }

            STARTUPINFOW startupInfo = {};
            startupInfo.cb = sizeof(startupInfo);

            PROCESS_INFORMATION processInfo = {};
            const BOOL created = CreateProcessW(applicationPath.c_str(),
                                                commandLine.data(),
                                                nullptr,
                                                nullptr,
                                                FALSE,
                                                0,
                                                nullptr,
                                                workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
                                                &startupInfo,
                                                &processInfo);
            if (created == FALSE)
            {
                VE_LOG_ERROR_CATEGORY("Editor", "Failed to start process with Win32 error {}: {}", GetLastError(), commandLineText);
                return -1;
            }

            WaitForSingleObject(processInfo.hProcess, INFINITE);

            DWORD exitCode = 1;
            const BOOL gotExitCode = GetExitCodeProcess(processInfo.hProcess, &exitCode);
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
            return gotExitCode != FALSE ? static_cast<int>(exitCode) : -1;
#else
            return std::system(commandLineText.c_str());
#endif
        }

        [[nodiscard]] Path ResolveProjectRelativeReference(std::string_view reference, std::string_view fallbackExtension)
        {
            if (reference.empty())
            {
                return Path();
            }

            std::string path(reference);
            if (!path.starts_with("Assets/"))
            {
                path = "Assets/" + path;
            }

            if (Path(path).GetExtension().empty() && !fallbackExtension.empty())
            {
                path += fallbackExtension;
            }

            return Path(path);
        }

        [[nodiscard]] Path ResolveSourcePath(const Path& projectRoot, const Path& descriptorProjectPath, std::string_view source)
        {
            if (source.empty())
            {
                return Path();
            }

            const Path sourcePath(source);
            if (sourcePath.IsAbsolute())
            {
                return sourcePath;
            }

            if (sourcePath.GetString().starts_with("Assets/") || sourcePath.GetString().starts_with("Library/"))
            {
                return projectRoot / sourcePath;
            }

            return projectRoot / descriptorProjectPath.GetParentPath() / sourcePath;
        }

        [[nodiscard]] Path GetBuiltinFxcPath(const Path& repositoryRoot)
        {
            return repositoryRoot / "ThirdParty/WindowsSdkTools/Tools/x64/fxc.exe";
        }

        [[nodiscard]] boost::json::array WriteVector3(const std::array<double, 3>& value)
        {
            boost::json::array array;
            array.emplace_back(value[0]);
            array.emplace_back(value[1]);
            array.emplace_back(value[2]);
            return array;
        }

        [[nodiscard]] std::array<double, 3> Subtract(const std::array<double, 3>& left, const std::array<double, 3>& right) noexcept
        {
            return {
                left[0] - right[0],
                left[1] - right[1],
                left[2] - right[2],
            };
        }

        [[nodiscard]] std::array<double, 3> Cross(const std::array<double, 3>& left, const std::array<double, 3>& right) noexcept
        {
            return {
                (left[1] * right[2]) - (left[2] * right[1]),
                (left[2] * right[0]) - (left[0] * right[2]),
                (left[0] * right[1]) - (left[1] * right[0]),
            };
        }

        [[nodiscard]] std::array<double, 3> Normalize(const std::array<double, 3>& value) noexcept
        {
            const double lengthSquared = (value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]);
            if (lengthSquared <= 1.0e-12)
            {
                return {0.0, 1.0, 0.0};
            }

            const double inverseLength = 1.0 / std::sqrt(lengthSquared);
            return {
                value[0] * inverseLength,
                value[1] * inverseLength,
                value[2] * inverseLength,
            };
        }

        [[nodiscard]] std::array<double, 3>
        CalculateFaceNormal(const std::array<double, 3>& a, const std::array<double, 3>& b, const std::array<double, 3>& c) noexcept
        {
            return Normalize(Cross(Subtract(b, a), Subtract(c, a)));
        }

        [[nodiscard]] boost::json::array WriteVertices(const std::vector<std::array<double, 3>>& vertices)
        {
            boost::json::array array;
            for (const std::array<double, 3>& vertex : vertices)
            {
                array.emplace_back(WriteVector3(vertex));
            }

            return array;
        }

        [[nodiscard]] boost::json::array WriteIndices(const std::vector<UInt32>& indices)
        {
            boost::json::array array;
            for (UInt32 index : indices)
            {
                array.emplace_back(index);
            }

            return array;
        }

        [[nodiscard]] Result<UInt32> ParseObjVertexIndex(std::string_view token, SizeT vertexCount)
        {
            const size_t slash = token.find('/');
            const std::string indexText(slash == std::string_view::npos ? token : token.substr(0, slash));
            if (indexText.empty())
            {
                return Result<UInt32>::Failure(Error(ErrorCode::InvalidArgument, "OBJ face has an empty vertex index."));
            }

            int objIndex = 0;
            try
            {
                objIndex = std::stoi(indexText);
            }
            catch (const std::exception&)
            {
                return Result<UInt32>::Failure(Error(ErrorCode::InvalidArgument, "OBJ face has an invalid vertex index: " + indexText));
            }

            const int zeroBasedIndex = objIndex > 0 ? objIndex - 1 : static_cast<int>(vertexCount) + objIndex;
            if (zeroBasedIndex < 0 || static_cast<SizeT>(zeroBasedIndex) >= vertexCount)
            {
                return Result<UInt32>::Failure(Error(ErrorCode::InvalidArgument, "OBJ face vertex index is outside the vertex array."));
            }

            return Result<UInt32>::Success(static_cast<UInt32>(zeroBasedIndex));
        }

        void UpdateBounds(ObjMeshData& mesh)
        {
            std::array<double, 3> minimum = {
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::max(),
            };
            std::array<double, 3> maximum = {
                std::numeric_limits<double>::lowest(),
                std::numeric_limits<double>::lowest(),
                std::numeric_limits<double>::lowest(),
            };

            for (const std::array<double, 3>& vertex : mesh.vertices)
            {
                for (SizeT axis = 0; axis < 3; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], vertex[axis]);
                    maximum[axis] = std::max(maximum[axis], vertex[axis]);
                }
            }

            for (SizeT axis = 0; axis < 3; ++axis)
            {
                mesh.boundsCenter[axis] = (minimum[axis] + maximum[axis]) * 0.5;
                mesh.boundsExtents[axis] = (maximum[axis] - minimum[axis]) * 0.5;
            }
        }

        [[nodiscard]] Result<ObjMeshData> ParseObjMesh(const Path& objPhysicalPath, const Path& objProjectPath)
        {
            Result<std::string> text = FileSystem::ReadTextFile(objPhysicalPath);
            if (!text)
            {
                return Result<ObjMeshData>::Failure(text.GetError());
            }

            ObjMeshData mesh;
            mesh.name = GetStem(objProjectPath);
            std::vector<std::array<double, 3>> sourceVertices;

            std::istringstream input(text.GetValue());
            std::string line;
            while (std::getline(input, line))
            {
                std::istringstream lineInput(line);
                std::string command;
                lineInput >> command;

                if (command == "o" || command == "g")
                {
                    std::string name;
                    lineInput >> name;
                    if (!name.empty())
                    {
                        mesh.name = name;
                    }
                }
                else if (command == "v")
                {
                    std::array<double, 3> vertex = {0.0, 0.0, 0.0};
                    if (!(lineInput >> vertex[0] >> vertex[1] >> vertex[2]))
                    {
                        return Result<ObjMeshData>::Failure(Error(ErrorCode::InvalidArgument, "OBJ vertex line must contain three numeric values."));
                    }
                    sourceVertices.push_back(vertex);
                }
                else if (command == "f")
                {
                    std::vector<UInt32> faceIndices;
                    std::string token;
                    while (lineInput >> token)
                    {
                        Result<UInt32> indexResult = ParseObjVertexIndex(token, sourceVertices.size());
                        if (!indexResult)
                        {
                            return Result<ObjMeshData>::Failure(indexResult.GetError());
                        }

                        faceIndices.push_back(indexResult.GetValue());
                    }

                    if (faceIndices.size() < 3)
                    {
                        return Result<ObjMeshData>::Failure(Error(ErrorCode::InvalidArgument, "OBJ face line must contain at least three vertices."));
                    }

                    for (SizeT index = 1; index + 1 < faceIndices.size(); ++index)
                    {
                        const std::array<double, 3>& a = sourceVertices[faceIndices[0]];
                        const std::array<double, 3>& b = sourceVertices[faceIndices[index]];
                        const std::array<double, 3>& c = sourceVertices[faceIndices[index + 1]];
                        const std::array<double, 3> normal = CalculateFaceNormal(a, b, c);
                        const UInt32 firstVertex = static_cast<UInt32>(mesh.vertices.size());

                        mesh.vertices.push_back(a);
                        mesh.vertices.push_back(b);
                        mesh.vertices.push_back(c);
                        mesh.normals.push_back(normal);
                        mesh.normals.push_back(normal);
                        mesh.normals.push_back(normal);
                        mesh.indices.push_back(firstVertex);
                        mesh.indices.push_back(firstVertex + 1);
                        mesh.indices.push_back(firstVertex + 2);
                    }
                }
            }

            if (mesh.vertices.empty() || mesh.indices.empty())
            {
                return Result<ObjMeshData>::Failure(Error(ErrorCode::InvalidArgument, "OBJ mesh must contain vertices and faces."));
            }

            UpdateBounds(mesh);
            return Result<ObjMeshData>::Success(std::move(mesh));
        }

        [[nodiscard]] boost::json::object BuildMetaJson(const EditorAssetRecord& record)
        {
            boost::json::object object;
            object["version"] = 1;
            object["guid"] = record.asset.id.GetGuid().ToString();
            object["assetType"] = ToMetaAssetType(record.type);
            object["sourcePath"] = record.path.GetString();
            object["importer"] = GetDefaultImporter(record.type);
            object["importSettings"] = boost::json::object{};
            return object;
        }

        [[nodiscard]] boost::json::object BuildMeshAssetJson(const ObjMeshData& mesh, const Guid& guid)
        {
            boost::json::object object;
            object["version"] = 1;
            object["type"] = "Mesh";
            object["guid"] = guid.ToString();
            object["name"] = mesh.name;
            object["vertexFormat"] = "PositionNormal";
            object["vertices"] = WriteVertices(mesh.vertices);
            object["normals"] = WriteVertices(mesh.normals);
            object["indices"] = WriteIndices(mesh.indices);
            object["boundsCenter"] = WriteVector3(mesh.boundsCenter);
            object["boundsExtents"] = WriteVector3(mesh.boundsExtents);
            object["importer"] = "ObjMeshImporter";
            object["importSettings"] = boost::json::object{};

            boost::json::object submesh;
            submesh["name"] = mesh.name;
            submesh["indexStart"] = 0;
            submesh["indexCount"] = mesh.indices.size();

            boost::json::array submeshes;
            submeshes.emplace_back(std::move(submesh));
            object["submeshes"] = std::move(submeshes);
            return object;
        }

        [[nodiscard]] Path MakeProjectRelativePath(const Path& projectRoot, const Path& path)
        {
            if (!path.IsAbsolute())
            {
                const Path relativeToProject = projectRoot / path;
                if (FileSystem::IsFile(relativeToProject))
                {
                    return path;
                }

                const std::string projectDirectoryName = projectRoot.GetFilename();
                const std::string pathText = path.GetString();
                const std::string projectDirectoryPrefix = projectDirectoryName + "/";
                if (!projectDirectoryName.empty() && pathText.starts_with(projectDirectoryPrefix) &&
                    FileSystem::IsFile(projectRoot.GetParentPath() / path))
                {
                    return Path(pathText.substr(projectDirectoryPrefix.size()));
                }

                return path;
            }

            const std::string& root = projectRoot.GetString();
            const std::string& pathText = path.GetString();
            const std::string prefix = root.ends_with('/') ? root : root + "/";
            if (pathText.starts_with(prefix))
            {
                return Path(pathText.substr(prefix.size()));
            }

            return path;
        }

        [[nodiscard]] ErrorCode RewriteShaderArtifactPaths(const Path& projectRoot, const Path& shaderRuntimePhysicalPath)
        {
            Result<std::string> text = FileSystem::ReadTextFile(shaderRuntimePhysicalPath);
            if (!text)
            {
                return text.GetError().GetCode();
            }

            Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
            if (!json || !json.GetValue().is_object())
            {
                return ErrorCode::InvalidArgument;
            }

            boost::json::object& object = json.GetValue().as_object();
            boost::json::value* stagesValue = object.if_contains("stages");
            if (stagesValue == nullptr || !stagesValue->is_array())
            {
                return ErrorCode::InvalidArgument;
            }

            for (boost::json::value& stageValue : stagesValue->as_array())
            {
                if (!stageValue.is_object())
                {
                    continue;
                }

                boost::json::value* artifactsValue = stageValue.as_object().if_contains("artifacts");
                if (artifactsValue == nullptr || !artifactsValue->is_object())
                {
                    continue;
                }

                boost::json::object& artifacts = artifactsValue->as_object();
                for (const char* key : {"d3d11", "d3d12", "spirv", "metal", "reflection"})
                {
                    boost::json::value* artifactValue = artifacts.if_contains(key);
                    if (artifactValue == nullptr || !artifactValue->is_string())
                    {
                        continue;
                    }

                    artifacts[key] = MakeProjectRelativePath(projectRoot, Path(std::string(artifactValue->as_string()))).GetString();
                }
            }

            object["source"] = MakeProjectRelativePath(projectRoot, Path(ReadString(object, "source"))).GetString();
            return FileSystem::WriteTextFile(shaderRuntimePhysicalPath, JsonUtils::SerializePretty(json.GetValue()));
        }

        [[nodiscard]] AssetID ReadDependencyIDFromMeta(const Path& metaPhysicalPath)
        {
            Result<std::string> text = FileSystem::ReadTextFile(metaPhysicalPath);
            if (!text)
            {
                return AssetID();
            }

            Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
            if (!json || !json.GetValue().is_object())
            {
                return AssetID();
            }

            Result<Guid> guid = Guid::Parse(ReadString(json.GetValue().as_object(), "guid"));
            if (!guid || guid.GetValue().IsEmpty())
            {
                return AssetID();
            }

            return AssetID(guid.MoveValue(), 0);
        }

        [[nodiscard]] bool IsFileNewerThan(const Path& left, const Path& right)
        {
            namespace fs = std::filesystem;

            std::error_code errorCode;
            const fs::file_time_type leftWriteTime = fs::last_write_time(fs::path(left.GetString()), errorCode);
            if (errorCode)
            {
                return true;
            }

            const fs::file_time_type rightWriteTime = fs::last_write_time(fs::path(right.GetString()), errorCode);
            if (errorCode)
            {
                return true;
            }

            return leftWriteTime > rightWriteTime;
        }

        [[nodiscard]] bool ShouldImportShader(
            const Path& shaderDescriptorPhysicalPath, const Path& shaderSourcePhysicalPath, const Path& shaderRuntimePhysicalPath, bool force)
        {
            if (force || !FileSystem::IsFile(shaderRuntimePhysicalPath))
            {
                return true;
            }

            return IsFileNewerThan(shaderDescriptorPhysicalPath, shaderRuntimePhysicalPath) || IsFileNewerThan(shaderSourcePhysicalPath, shaderRuntimePhysicalPath);
        }

        [[nodiscard]] ErrorCode MergeShaderMaterialLayout(
            const boost::json::object& shaderDescriptor, const Path& shaderRuntimePhysicalPath)
        {
            Result<std::string> runtimeText = FileSystem::ReadTextFile(shaderRuntimePhysicalPath);
            if (!runtimeText)
            {
                return runtimeText.GetError().GetCode();
            }

            Result<boost::json::value> runtimeJson = JsonUtils::Parse(runtimeText.GetValue());
            if (!runtimeJson || !runtimeJson.GetValue().is_object())
            {
                return ErrorCode::InvalidArgument;
            }

            Result<ShaderMaterialLayout> layout = ReadShaderMaterialLayoutJson(shaderDescriptor);
            if (!layout)
            {
                return layout.GetError().GetCode();
            }

            runtimeJson.GetValue().as_object()["material"] = WriteShaderMaterialLayoutJson(layout.GetValue());
            return FileSystem::WriteTextFile(shaderRuntimePhysicalPath, JsonUtils::SerializePretty(runtimeJson.GetValue()));
        }

        void AddDependencyIfValid(std::vector<AssetID>& dependencies, const AssetID& id)
        {
            if (id.IsEmpty())
            {
                return;
            }

            if (std::find(dependencies.begin(), dependencies.end(), id) == dependencies.end())
            {
                dependencies.push_back(id);
            }
        }

        void ResolveMaterialTextureDependencies(const boost::json::object& materialProperties, const ShaderMaterialLayout& layout, std::vector<AssetID>& dependencies)
        {
            for (const ShaderMaterialPropertyDesc& property : layout.properties)
            {
                if (property.type != MaterialPropertyType::Texture2D)
                {
                    continue;
                }

                const boost::json::value* value = materialProperties.if_contains(property.name);
                if (value == nullptr || value->is_null())
                {
                    continue;
                }

                Result<MaterialPropertyValue> propertyValue = ReadMaterialPropertyValueJson(property.type, *value);
                if (propertyValue && !propertyValue.GetValue().assetValue.IsEmpty())
                {
                    AddDependencyIfValid(dependencies, propertyValue.GetValue().assetValue);
                }
            }
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
        assetsByID_.clear();
        assetIDsByAssetPath_.clear();
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

        assetsByID_.clear();
        assetIDsByAssetPath_.clear();
        const ErrorCode result = ScanAndImportDirectory(assetsRoot, false);
        if (result != ErrorCode::None)
        {
            return result;
        }
        return ResolveAssetDependencies();
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

        assetsByID_.clear();
        assetIDsByAssetPath_.clear();
        const ErrorCode result = ScanAndImportDirectory(assetsRoot, true);
        if (result != ErrorCode::None)
        {
            return result;
        }
        return ResolveAssetDependencies();
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

        if (asset->type == EditorAssetType::ObjSource)
        {
            const ErrorCode result = ImportObjAsMesh(asset->path, asset->asset.id.GetGuid(), true);
            if (result != ErrorCode::None)
            {
                return result;
            }
        }
        else if (asset->type == EditorAssetType::Shader)
        {
            const ErrorCode result = ImportShader(asset->path, asset->asset.id.GetGuid(), true);
            if (result != ErrorCode::None)
            {
                return result;
            }
        }

        const ErrorCode refreshResult = Refresh();
        if (refreshResult != ErrorCode::None)
        {
            return refreshResult;
        }
        return ErrorCode::None;
    }

    SizeT EditorAssetDatabase::GetAssetCount() const noexcept
    {
        return assetsByID_.size();
    }

    const EditorAssetRecord* EditorAssetDatabase::FindAsset(const Path& projectRelativePath) const noexcept
    {
        const auto idIt = assetIDsByAssetPath_.find(projectRelativePath.GetString());
        if (idIt == assetIDsByAssetPath_.end())
        {
            return nullptr;
        }

        return FindAssetByID(idIt->second);
    }

    const EditorAssetRecord* EditorAssetDatabase::FindAssetByID(const AssetID& id) const noexcept
    {
        const auto assetIt = assetsByID_.find(id);
        return assetIt != assetsByID_.end() ? &assetIt->second : nullptr;
    }

    Result<AssetRecord> EditorAssetDatabase::FindAssetRecord(const AssetID& id) const
    {
        if (!initialized_)
        {
            return Result<AssetRecord>::Failure(Error(ErrorCode::InvalidState, "EditorAssetDatabase is not initialized."));
        }

        const EditorAssetRecord* asset = FindAssetByID(id);
        if (asset == nullptr)
        {
            return Result<AssetRecord>::Failure(Error(ErrorCode::NotFound, "Editor asset not found."));
        }

        return Result<AssetRecord>::Success(asset->asset);
    }

    const std::unordered_map<AssetID, EditorAssetRecord>& EditorAssetDatabase::GetAssetsByID() const noexcept
    {
        return assetsByID_;
    }

    const std::unordered_map<std::string, AssetID>& EditorAssetDatabase::GetAssetIDsByAssetPath() const noexcept
    {
        return assetIDsByAssetPath_;
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
        case EditorAssetType::Shader:
            return "Shader";
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
            else if (entry.type == FileSystem::DirectoryEntryType::File && IsSupportedNativeAssetFile(entry.path))
            {
                EditorAssetRecord record;
                record.path = ToProjectRelativePath(entry.path);
                record.metaPath = GetMetaPath(record.path);
                record.type = GetAssetTypeFromExtension(entry.path);

                Result<Guid> guidResult = EnsureMeta(record);
                if (!guidResult)
                {
                    return guidResult.GetError().GetCode();
                }

                const Guid guid = guidResult.MoveValue();
                record.asset.id = AssetID(guid, 0);
                record.asset.type = ToResourceType(record.type);
                if (record.type == EditorAssetType::ObjSource)
                {
                    const ErrorCode importResult = ImportObjAsMesh(record.path, guid, force);
                    if (importResult != ErrorCode::None)
                    {
                        return importResult;
                    }

                    record.imported = true;
                    record.importedPath = GetImportedMeshPath(guid, record.path);
                }
                else if (record.type == EditorAssetType::Shader)
                {
                    Result<std::string> shaderName = ReadShaderName(record.path);
                    if (!shaderName)
                    {
                        return shaderName.GetError().GetCode();
                    }

                    const ErrorCode importResult = ImportShader(record.path, guid, force);
                    if (importResult != ErrorCode::None)
                    {
                        return importResult;
                    }

                    record.imported = true;
                    record.importedPath = GetImportedShaderPath(guid, shaderName.GetValue());
                }
                record.asset.runtimePath = record.imported ? record.importedPath : record.path;

                AddAssetRecord(std::move(record));
            }
        }

        return ErrorCode::None;
    }

    ErrorCode EditorAssetDatabase::ImportObjAsMesh(const Path& objProjectPath, const Guid& guid, bool force)
    {
        if (objProjectPath.IsEmpty() || guid.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const Path meshProjectPath = GetImportedMeshPath(guid, objProjectPath);
        const Path meshPhysicalPath = projectRoot_ / meshProjectPath;
        if (!force && FileSystem::IsFile(meshPhysicalPath))
        {
            return ErrorCode::None;
        }

        Result<ObjMeshData> meshData = ParseObjMesh(projectRoot_ / objProjectPath, objProjectPath);
        if (!meshData)
        {
            return meshData.GetError().GetCode();
        }

        const boost::json::value meshJson(BuildMeshAssetJson(meshData.GetValue(), guid));
        return FileSystem::WriteTextFile(meshPhysicalPath, JsonUtils::SerializePretty(meshJson));
    }

    ErrorCode EditorAssetDatabase::ImportShader(const Path& shaderProjectPath, const Guid& guid, bool force)
    {
        if (shaderProjectPath.IsEmpty() || guid.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        Result<std::string> shaderNameResult = ReadShaderName(shaderProjectPath);
        if (!shaderNameResult)
        {
            return shaderNameResult.GetError().GetCode();
        }

        const std::string shaderName = shaderNameResult.GetValue();
        const Path shaderRuntimePath = GetImportedShaderPath(guid, shaderName);
        const Path shaderRuntimePhysicalPath = projectRoot_ / shaderRuntimePath;
        const Path shaderDescriptorPhysicalPath = projectRoot_ / shaderProjectPath;
        Result<std::string> descriptorText = FileSystem::ReadTextFile(shaderDescriptorPhysicalPath);
        if (!descriptorText)
        {
            return descriptorText.GetError().GetCode();
        }

        Result<boost::json::value> descriptorJson = JsonUtils::Parse(descriptorText.GetValue());
        if (!descriptorJson || !descriptorJson.GetValue().is_object())
        {
            return ErrorCode::InvalidArgument;
        }

        const boost::json::object& descriptor = descriptorJson.GetValue().as_object();
        const Path sourcePath = ResolveSourcePath(projectRoot_, shaderProjectPath, ReadString(descriptor, "source"));
        if (shaderName.empty() || sourcePath.IsEmpty() || !FileSystem::IsFile(sourcePath))
        {
            return ErrorCode::NotFound;
        }

        if (!ShouldImportShader(shaderDescriptorPhysicalPath, sourcePath, shaderRuntimePhysicalPath, force))
        {
            const ErrorCode rewriteResult = RewriteShaderArtifactPaths(projectRoot_, shaderRuntimePhysicalPath);
            if (rewriteResult != ErrorCode::None)
            {
                return rewriteResult;
            }

            return MergeShaderMaterialLayout(descriptor, shaderRuntimePhysicalPath);
        }

        const Path outputDirectory = projectRoot_ / GetImportedShaderDirectory(guid);
        const Path executableDirectory = FileSystem::GetExecutableDirectory();
        const Path shaderToolPath = executableDirectory / "VEngineShaderTool.exe";
        if (!FileSystem::IsFile(shaderToolPath))
        {
            return ErrorCode::NotFound;
        }

        std::vector<std::string> arguments = {
            shaderToolPath.GetString(),
            "compile",
            "--source",
            sourcePath.GetString(),
            "--output",
            outputDirectory.GetString(),
            "--name",
            shaderName,
        };

        const Path repositoryRoot = executableDirectory.GetParentPath().GetParentPath().GetParentPath();
        const Path dxcPath = repositoryRoot / "ThirdParty/DirectXShaderCompiler/Build/Windows64/1.9.2602.17/Tools/x64/dxc.exe";
        if (FileSystem::IsFile(dxcPath))
        {
            arguments.push_back("--dxc");
            arguments.push_back(dxcPath.GetString());
        }

        const Path fxcPath = GetBuiltinFxcPath(repositoryRoot);
        if (!FileSystem::IsFile(fxcPath))
        {
            VE_LOG_ERROR_CATEGORY("Editor", "Builtin fxc.exe was not found: {}", fxcPath.GetString());
            return ErrorCode::NotFound;
        }

        arguments.push_back("--fxc");
        arguments.push_back(fxcPath.GetString());

        const Path spirvCrossPath = repositoryRoot / "ThirdParty/SPIRV-Cross/Build/Windows64/vulkan-sdk-1.4.309.0/Release/spirv-cross.exe";
        if (FileSystem::IsFile(spirvCrossPath))
        {
            arguments.push_back("--spirv-cross");
            arguments.push_back(spirvCrossPath.GetString());
        }

        const int shaderToolExitCode = RunProcess(arguments);
        if (shaderToolExitCode != 0)
        {
            return ErrorCode::IOError;
        }

        if (!FileSystem::IsFile(shaderRuntimePhysicalPath))
        {
            return ErrorCode::NotFound;
        }

        const ErrorCode rewriteResult = RewriteShaderArtifactPaths(projectRoot_, shaderRuntimePhysicalPath);
        if (rewriteResult != ErrorCode::None)
        {
            return rewriteResult;
        }

        return MergeShaderMaterialLayout(descriptor, shaderRuntimePhysicalPath);
    }

    ErrorCode EditorAssetDatabase::ResolveAssetDependencies()
    {
        for (auto& pair : assetsByID_)
        {
            EditorAssetRecord& record = pair.second;
            record.asset.dependencies.clear();

            if (record.type != EditorAssetType::Material)
            {
                continue;
            }

            Result<std::string> materialText = FileSystem::ReadTextFile(projectRoot_ / record.path);
            if (!materialText)
            {
                return materialText.GetError().GetCode();
            }

            Result<boost::json::value> materialJson = JsonUtils::Parse(materialText.GetValue());
            if (!materialJson || !materialJson.GetValue().is_object())
            {
                continue;
            }

            const Path shaderProjectPath = ResolveProjectRelativeReference(ReadString(materialJson.GetValue().as_object(), "shader"), ".veshader");
            if (shaderProjectPath.IsEmpty())
            {
                continue;
            }

            AssetID shaderDependencyID;
            const EditorAssetRecord* shaderAsset = nullptr;
            const auto shaderID = assetIDsByAssetPath_.find(shaderProjectPath.GetString());
            if (shaderID != assetIDsByAssetPath_.end())
            {
                shaderDependencyID = shaderID->second;
                shaderAsset = FindAssetByID(shaderDependencyID);
            }
            else
            {
                shaderDependencyID = ReadDependencyIDFromMeta(projectRoot_ / GetMetaPath(shaderProjectPath));
                shaderAsset = FindAssetByID(shaderDependencyID);
            }

            if (shaderDependencyID.IsEmpty() || shaderAsset == nullptr)
            {
                continue;
            }

            AddDependencyIfValid(record.asset.dependencies, shaderDependencyID);

            Result<std::string> shaderText = FileSystem::ReadTextFile(projectRoot_ / shaderAsset->asset.runtimePath);
            if (!shaderText)
            {
                return shaderText.GetError().GetCode();
            }

            Result<boost::json::value> shaderJson = JsonUtils::Parse(shaderText.GetValue());
            if (!shaderJson || !shaderJson.GetValue().is_object())
            {
                return ErrorCode::InvalidArgument;
            }

            Result<ShaderMaterialLayout> layout = ReadShaderMaterialLayoutJson(shaderJson.GetValue().as_object());
            if (!layout)
            {
                return layout.GetError().GetCode();
            }

            const boost::json::value* propertiesValue = materialJson.GetValue().as_object().if_contains("properties");
            if (propertiesValue == nullptr || !propertiesValue->is_object())
            {
                return ErrorCode::InvalidArgument;
            }

            ResolveMaterialTextureDependencies(propertiesValue->as_object(), layout.GetValue(), record.asset.dependencies);
        }

        return ErrorCode::None;
    }

    Result<Guid> EditorAssetDatabase::EnsureMeta(const EditorAssetRecord& record) const
    {
        const Path metaPhysicalPath = projectRoot_ / record.metaPath;
        if (FileSystem::IsFile(metaPhysicalPath))
        {
            Result<Guid> guidResult = ReadMetaGuid(metaPhysicalPath);
            if (guidResult && !guidResult.GetValue().IsEmpty())
            {
                return guidResult;
            }
        }

        EditorAssetRecord metaRecord = record;
        metaRecord.asset.id = AssetID(Guid::Create(), 0);

        const boost::json::value metaJson(BuildMetaJson(metaRecord));
        const ErrorCode result = FileSystem::WriteTextFile(metaPhysicalPath, JsonUtils::SerializePretty(metaJson));
        if (result != ErrorCode::None)
        {
            return Result<Guid>::Failure(Error(result, "Failed to write asset meta file."));
        }

        return Result<Guid>::Success(metaRecord.asset.id.GetGuid());
    }

    Result<Guid> EditorAssetDatabase::ReadMetaGuid(const Path& metaPhysicalPath) const
    {
        Result<std::string> text = FileSystem::ReadTextFile(metaPhysicalPath);
        if (!text)
        {
            return Result<Guid>::Failure(text.GetError());
        }

        Result<boost::json::value> json = JsonUtils::Parse(text.GetValue());
        if (!json)
        {
            return Result<Guid>::Failure(json.GetError());
        }

        if (!json.GetValue().is_object())
        {
            return Result<Guid>::Failure(Error(ErrorCode::InvalidArgument, "Asset meta descriptor root must be a JSON object."));
        }

        Result<Guid> guid = Guid::Parse(ReadString(json.GetValue().as_object(), "guid"));
        if (!guid)
        {
            return guid;
        }

        return guid;
    }

    Path EditorAssetDatabase::GetImportedMeshPath(const Guid& guid, const Path& objProjectPath) const
    {
        return Path(EditorProject::LibraryDirectoryName) / ImportedDirectoryName / guid.ToString() / GetImportedMeshFilename(objProjectPath);
    }

    Path EditorAssetDatabase::GetImportedShaderDirectory(const Guid& guid) const
    {
        return Path(EditorProject::LibraryDirectoryName) / ImportedDirectoryName / guid.ToString();
    }

    Path EditorAssetDatabase::GetImportedShaderPath(const Guid& guid, std::string_view shaderName) const
    {
        return GetImportedShaderDirectory(guid) / GetImportedShaderFilename(shaderName);
    }

    Result<std::string> EditorAssetDatabase::ReadShaderName(const Path& shaderProjectPath) const
    {
        Result<std::string> descriptorText = FileSystem::ReadTextFile(projectRoot_ / shaderProjectPath);
        if (!descriptorText)
        {
            return descriptorText;
        }

        Result<boost::json::value> descriptorJson = JsonUtils::Parse(descriptorText.GetValue());
        if (!descriptorJson || !descriptorJson.GetValue().is_object())
        {
            return Result<std::string>::Failure(Error(ErrorCode::InvalidArgument, "Shader source descriptor root must be a JSON object."));
        }

        const std::string shaderName = ReadString(descriptorJson.GetValue().as_object(), "name", GetStem(shaderProjectPath));
        if (shaderName.empty())
        {
            return Result<std::string>::Failure(Error(ErrorCode::InvalidArgument, "Shader source descriptor requires a non-empty shader name."));
        }

        return Result<std::string>::Success(shaderName);
    }

    Path EditorAssetDatabase::GetMetaPath(const Path& assetProjectPath) const
    {
        return Path(assetProjectPath.GetString() + ".meta");
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
        const std::string pathKey = record.path.GetString();
        const AssetID idKey = record.asset.id;

        if (const auto existingID = assetIDsByAssetPath_.find(pathKey); existingID != assetIDsByAssetPath_.end() && existingID->second != idKey)
        {
            assetsByID_.erase(existingID->second);
        }

        if (const auto existingAsset = assetsByID_.find(idKey); existingAsset != assetsByID_.end())
        {
            assetIDsByAssetPath_.erase(existingAsset->second.path.GetString());
        }

        assetsByID_.insert_or_assign(idKey, std::move(record));
        assetIDsByAssetPath_.insert_or_assign(pathKey, idKey);
    }
} // namespace ve::editor
