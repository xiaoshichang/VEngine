#include "Editor/Core/EditorAssetDatabase.h"

#include "Editor/Core/EditorProject.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <algorithm>
#include <array>
#include <boost/json.hpp>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

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
            case EditorAssetType::Scene:
                return "Scene";
            case EditorAssetType::Unknown:
                break;
            }

            return "Unknown";
        }

        [[nodiscard]] const char* GetDefaultImporter(EditorAssetType type) noexcept
        {
            return type == EditorAssetType::ObjSource ? "ObjMeshImporter" : "";
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
        return ErrorCode::None;
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
        return ErrorCode::None;
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
