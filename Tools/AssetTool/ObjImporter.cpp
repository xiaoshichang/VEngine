#include "Tools/AssetTool/ObjImporter.h"

#include "Engine/Runtime/Asset/NativeAssetIO.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"

#include <algorithm>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>

namespace ve
{
    namespace
    {
        struct ObjVertexRef
        {
            Int32 position = 0;
            Int32 normal = 0;
        };

        struct ObjModelData
        {
            std::vector<Vector3> positions;
            std::vector<Vector3> normals;
            std::vector<MeshVertex> vertices;
        };

        [[nodiscard]] std::string Trim(std::string_view text)
        {
            const SizeT first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
            {
                return {};
            }

            const SizeT last = text.find_last_not_of(" \t\r\n");
            return std::string(text.substr(first, last - first + 1));
        }

        [[nodiscard]] Result<Float32> ParseFloat(std::string_view text)
        {
            try
            {
                SizeT consumed = 0;
                const float value = std::stof(std::string(text), &consumed);
                if (consumed != text.size())
                {
                    return Result<Float32>::Failure(Error(ErrorCode::InvalidArgument, "Invalid OBJ float."));
                }

                return Result<Float32>::Success(value);
            }
            catch (...)
            {
                return Result<Float32>::Failure(Error(ErrorCode::InvalidArgument, "Invalid OBJ float."));
            }
        }

        [[nodiscard]] Result<Int32> ParseInt(std::string_view text)
        {
            if (text.empty())
            {
                return Result<Int32>::Success(0);
            }

            try
            {
                SizeT consumed = 0;
                const int value = std::stoi(std::string(text), &consumed);
                if (consumed != text.size())
                {
                    return Result<Int32>::Failure(Error(ErrorCode::InvalidArgument, "Invalid OBJ index."));
                }

                return Result<Int32>::Success(value);
            }
            catch (...)
            {
                return Result<Int32>::Failure(Error(ErrorCode::InvalidArgument, "Invalid OBJ index."));
            }
        }

        [[nodiscard]] Result<Vector3> ParseVector3Line(std::istringstream& stream)
        {
            std::string xText;
            std::string yText;
            std::string zText;
            stream >> xText >> yText >> zText;

            Result<Float32> x = ParseFloat(xText);
            Result<Float32> y = ParseFloat(yText);
            Result<Float32> z = ParseFloat(zText);
            if (!x || !y || !z)
            {
                return Result<Vector3>::Failure(Error(ErrorCode::InvalidArgument, "Invalid OBJ vector."));
            }

            return Result<Vector3>::Success(Vector3(x.GetValue(), y.GetValue(), z.GetValue()));
        }

        [[nodiscard]] Result<ObjVertexRef> ParseVertexRef(std::string_view token)
        {
            ObjVertexRef ref;

            const SizeT firstSlash = token.find('/');
            if (firstSlash == std::string_view::npos)
            {
                Result<Int32> position = ParseInt(token);
                if (!position)
                {
                    return Result<ObjVertexRef>::Failure(position.GetError());
                }

                ref.position = position.GetValue();
                return Result<ObjVertexRef>::Success(ref);
            }

            Result<Int32> position = ParseInt(token.substr(0, firstSlash));
            if (!position)
            {
                return Result<ObjVertexRef>::Failure(position.GetError());
            }

            ref.position = position.GetValue();

            const SizeT secondSlash = token.find('/', firstSlash + 1);
            if (secondSlash != std::string_view::npos)
            {
                Result<Int32> normal = ParseInt(token.substr(secondSlash + 1));
                if (!normal)
                {
                    return Result<ObjVertexRef>::Failure(normal.GetError());
                }

                ref.normal = normal.GetValue();
            }

            return Result<ObjVertexRef>::Success(ref);
        }

        [[nodiscard]] Result<SizeT> ResolveObjIndex(Int32 index, SizeT itemCount)
        {
            if (index > 0)
            {
                const SizeT resolved = static_cast<SizeT>(index - 1);
                if (resolved < itemCount)
                {
                    return Result<SizeT>::Success(resolved);
                }
            }
            else if (index < 0)
            {
                const Int64 resolved = static_cast<Int64>(itemCount) + index;
                if (resolved >= 0 && static_cast<SizeT>(resolved) < itemCount)
                {
                    return Result<SizeT>::Success(static_cast<SizeT>(resolved));
                }
            }

            return Result<SizeT>::Failure(Error(ErrorCode::InvalidArgument, "OBJ index is out of range."));
        }

        [[nodiscard]] Vector3 ComputeFaceNormal(const Vector3& a, const Vector3& b, const Vector3& c)
        {
            const Vector3 normal = Vector3::Cross(b - a, c - a).Normalized();
            return normal == Vector3::Zero() ? Vector3::UnitY() : normal;
        }

        [[nodiscard]] Result<MeshVertex> MakeVertex(const ObjModelData& model,
                                                    const ObjVertexRef& ref,
                                                    const Vector3& fallbackNormal)
        {
            Result<SizeT> positionIndex = ResolveObjIndex(ref.position, model.positions.size());
            if (!positionIndex)
            {
                return Result<MeshVertex>::Failure(positionIndex.GetError());
            }

            MeshVertex vertex;
            vertex.position = model.positions[positionIndex.GetValue()];
            vertex.normal = fallbackNormal;
            vertex.color = Vector3::One();

            if (ref.normal != 0)
            {
                Result<SizeT> normalIndex = ResolveObjIndex(ref.normal, model.normals.size());
                if (!normalIndex)
                {
                    return Result<MeshVertex>::Failure(normalIndex.GetError());
                }

                vertex.normal = model.normals[normalIndex.GetValue()];
            }

            return Result<MeshVertex>::Success(vertex);
        }

        [[nodiscard]] ErrorCode AddTriangle(ObjModelData& model,
                                             const ObjVertexRef& a,
                                             const ObjVertexRef& b,
                                             const ObjVertexRef& c)
        {
            Result<SizeT> positionA = ResolveObjIndex(a.position, model.positions.size());
            Result<SizeT> positionB = ResolveObjIndex(b.position, model.positions.size());
            Result<SizeT> positionC = ResolveObjIndex(c.position, model.positions.size());
            if (!positionA || !positionB || !positionC)
            {
                return ErrorCode::InvalidArgument;
            }

            const Vector3 fallbackNormal = ComputeFaceNormal(model.positions[positionA.GetValue()],
                                                             model.positions[positionB.GetValue()],
                                                             model.positions[positionC.GetValue()]);
            Result<MeshVertex> vertexA = MakeVertex(model, a, fallbackNormal);
            Result<MeshVertex> vertexB = MakeVertex(model, b, fallbackNormal);
            Result<MeshVertex> vertexC = MakeVertex(model, c, fallbackNormal);
            if (!vertexA || !vertexB || !vertexC)
            {
                return ErrorCode::InvalidArgument;
            }

            model.vertices.push_back(vertexA.GetValue());
            model.vertices.push_back(vertexB.GetValue());
            model.vertices.push_back(vertexC.GetValue());
            return ErrorCode::None;
        }

        [[nodiscard]] Result<ObjModelData> ParseObj(std::string_view sourceText)
        {
            ObjModelData model;
            std::istringstream sourceStream{std::string(sourceText)};
            std::string line;

            while (std::getline(sourceStream, line))
            {
                const std::string trimmed = Trim(line);
                if (trimmed.empty() || trimmed.starts_with('#'))
                {
                    continue;
                }

                std::istringstream lineStream(trimmed);
                std::string tag;
                lineStream >> tag;

                if (tag == "v")
                {
                    Result<Vector3> position = ParseVector3Line(lineStream);
                    if (!position)
                    {
                        return Result<ObjModelData>::Failure(position.GetError());
                    }

                    model.positions.push_back(position.GetValue());
                }
                else if (tag == "vn")
                {
                    Result<Vector3> normal = ParseVector3Line(lineStream);
                    if (!normal)
                    {
                        return Result<ObjModelData>::Failure(normal.GetError());
                    }

                    model.normals.push_back(normal.GetValue().Normalized());
                }
                else if (tag == "f")
                {
                    std::vector<ObjVertexRef> face;
                    std::string token;
                    while (lineStream >> token)
                    {
                        Result<ObjVertexRef> vertexRef = ParseVertexRef(token);
                        if (!vertexRef)
                        {
                            return Result<ObjModelData>::Failure(vertexRef.GetError());
                        }

                        face.push_back(vertexRef.GetValue());
                    }

                    if (face.size() < 3)
                    {
                        return Result<ObjModelData>::Failure(Error(ErrorCode::InvalidArgument, "OBJ face is invalid."));
                    }

                    for (SizeT index = 1; index + 1 < face.size(); ++index)
                    {
                        const ErrorCode result = AddTriangle(model, face[0], face[index], face[index + 1]);
                        if (result != ErrorCode::None)
                        {
                            return Result<ObjModelData>::Failure(
                                Error(result, "OBJ face could not be converted."));
                        }
                    }
                }
            }

            if (model.vertices.empty())
            {
                return Result<ObjModelData>::Failure(Error(ErrorCode::InvalidArgument, "OBJ produced no vertices."));
            }

            return Result<ObjModelData>::Success(std::move(model));
        }

        [[nodiscard]] Result<std::string> ComputeSourceHash(const Path& path)
        {
            Result<std::vector<std::byte>> dataResult = FileSystem::ReadBinaryFile(path);
            if (!dataResult)
            {
                return Result<std::string>::Failure(dataResult.GetError());
            }

            UInt64 hash = 14695981039346656037ull;
            for (std::byte byteValue : dataResult.GetValue())
            {
                hash ^= std::to_integer<UInt64>(byteValue);
                hash *= 1099511628211ull;
            }

            std::ostringstream stream;
            stream << std::hex << std::setfill('0') << std::setw(16) << hash;
            return Result<std::string>::Success(stream.str());
        }

        [[nodiscard]] std::string GetStem(const Path& path)
        {
            std::string filename = path.GetFilename();
            const SizeT dot = filename.find_last_of('.');
            if (dot != std::string::npos && dot > 0)
            {
                filename.erase(dot);
            }

            return filename.empty() ? "Mesh" : filename;
        }

        [[nodiscard]] bool HasSingleMeshArtifact(const SourceAssetMetadata& metadata, const Path& artifactPath)
        {
            return metadata.artifacts.size() == 1 && metadata.artifacts[0].type == "Mesh" &&
                   metadata.artifacts[0].path == artifactPath;
        }
    } // namespace

    Result<ObjImportResult> ImportObjModel(AssetDatabase& assetDatabase, const Path& sourcePath, bool force)
    {
        const Path relativeSource = assetDatabase.MakeProjectRelativePath(sourcePath);
        const Path absoluteSource = assetDatabase.ResolveProjectPath(relativeSource);
        if (!FileSystem::IsFile(absoluteSource))
        {
            return Result<ObjImportResult>::Failure(
                Error(ErrorCode::NotFound, "OBJ source file was not found: " + absoluteSource.GetString()));
        }

        Result<std::string> hash = ComputeSourceHash(absoluteSource);
        if (!hash)
        {
            return Result<ObjImportResult>::Failure(hash.GetError());
        }

        const Path metadataPath = assetDatabase.GetSourceMetadataPath(relativeSource);
        SourceAssetMetadata metadata;
        if (FileSystem::IsFile(assetDatabase.ResolveProjectPath(metadataPath)))
        {
            Result<SourceAssetMetadata> existingMetadata = assetDatabase.LoadSourceMetadata(metadataPath);
            if (!existingMetadata)
            {
                return Result<ObjImportResult>::Failure(existingMetadata.GetError());
            }

            metadata = existingMetadata.MoveValue();
        }
        else
        {
            metadata.guid = AssetGuid::Generate();
            metadata.assetType = AssetType::SourceModel;
            metadata.source = relativeSource;
            metadata.importer = "ObjModel";
            metadata.importerVersion = 1;
        }

        const std::string stem = GetStem(relativeSource);
        const Path meshArtifactPath =
            Path("Generated/Assets/ImportCache") / metadata.guid.ToString() / (stem + ".vemesh");
        const bool metadataAlreadyCurrent =
            metadata.assetType == AssetType::SourceModel && metadata.source == relativeSource &&
            metadata.sourceHash == hash.GetValue() && metadata.importer == "ObjModel" &&
            metadata.importerVersion == 1 && HasSingleMeshArtifact(metadata, meshArtifactPath);

        if (!force && metadata.sourceHash == hash.GetValue() &&
            FileSystem::IsFile(assetDatabase.ResolveProjectPath(meshArtifactPath)))
        {
            ObjImportResult result;
            result.guid = metadata.guid;
            result.metadataPath = metadataPath;
            result.meshArtifactPath = meshArtifactPath;
            result.sourceHash = metadata.sourceHash;
            if (Result<MeshAssetData> cachedMesh = LoadMeshAsset(assetDatabase.ResolveProjectPath(meshArtifactPath)))
            {
                result.vertexCount = cachedMesh.GetValue().vertices.size();
            }
            return Result<ObjImportResult>::Success(std::move(result));
        }

        Result<std::string> sourceText = FileSystem::ReadTextFile(absoluteSource);
        if (!sourceText)
        {
            return Result<ObjImportResult>::Failure(sourceText.GetError());
        }

        Result<ObjModelData> model = ParseObj(sourceText.GetValue());
        if (!model)
        {
            return Result<ObjImportResult>::Failure(model.GetError());
        }

        MeshAssetData mesh;
        mesh.sourceGuid = metadata.guid;
        mesh.name = stem;
        mesh.vertices = std::move(model.GetValue().vertices);

        const ErrorCode meshSaveResult = SaveMeshAsset(assetDatabase.ResolveProjectPath(meshArtifactPath), mesh);
        if (meshSaveResult != ErrorCode::None)
        {
            return Result<ObjImportResult>::Failure(Error(meshSaveResult, "Failed to write .vemesh artifact."));
        }

        metadata.assetType = AssetType::SourceModel;
        metadata.source = relativeSource;
        metadata.sourceHash = hash.GetValue();
        metadata.importer = "ObjModel";
        metadata.importerVersion = 1;
        metadata.artifacts.clear();
        metadata.artifacts.push_back(AssetArtifact{"Mesh", meshArtifactPath});

        if (!metadataAlreadyCurrent)
        {
            const ErrorCode metadataSaveResult = assetDatabase.SaveSourceMetadata(metadata);
            if (metadataSaveResult != ErrorCode::None)
            {
                return Result<ObjImportResult>::Failure(
                    Error(metadataSaveResult, "Failed to write source asset metadata."));
            }
        }

        const ErrorCode refreshResult = assetDatabase.Refresh();
        if (refreshResult != ErrorCode::None)
        {
            VE_LOG_WARN_CATEGORY("Asset", "AssetDatabase refresh after import returned {}.", ToString(refreshResult));
        }

        ObjImportResult result;
        result.guid = metadata.guid;
        result.metadataPath = metadataPath;
        result.meshArtifactPath = meshArtifactPath;
        result.sourceHash = metadata.sourceHash;
        result.vertexCount = mesh.vertices.size();
        return Result<ObjImportResult>::Success(std::move(result));
    }
} // namespace ve
