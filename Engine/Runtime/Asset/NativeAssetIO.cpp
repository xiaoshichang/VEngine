#include "Engine/Runtime/Asset/NativeAssetIO.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

namespace ve
{
    namespace
    {
        using boost::json::array;
        using boost::json::object;
        using boost::json::value;

        [[nodiscard]] Error MakeInvalidJsonError(const Path& path, const char* message)
        {
            return Error(ErrorCode::InvalidArgument, std::string(message) + ": " + path.GetString());
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
                return Result<object>::Failure(MakeInvalidJsonError(path, "Asset JSON root must be an object"));
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

            return AssetGuid::Parse(std::string_view(member->as_string().data(), member->as_string().size()));
        }

        [[nodiscard]] bool IsExpectedMeshVertexFormat(const value* jsonValue) noexcept
        {
            if (jsonValue == nullptr || !jsonValue->is_array())
            {
                return false;
            }

            const array& vertexFormat = jsonValue->as_array();
            if (vertexFormat.size() != 3)
            {
                return false;
            }

            return vertexFormat[0].is_string() && vertexFormat[0].as_string() == "position3" &&
                   vertexFormat[1].is_string() && vertexFormat[1].as_string() == "normal3" &&
                   vertexFormat[2].is_string() && vertexFormat[2].as_string() == "color3";
        }

        [[nodiscard]] value ToJson(const Vector3& vector)
        {
            return array{vector.GetX(), vector.GetY(), vector.GetZ()};
        }

        [[nodiscard]] std::string QuoteJsonString(std::string_view text)
        {
            return boost::json::serialize(value(boost::json::string(text)));
        }

        [[nodiscard]] std::string FormatFloat(Float32 value)
        {
            if (std::abs(value) < 0.0000001f)
            {
                value = 0.0f;
            }

            std::ostringstream stream;
            stream << std::setprecision(9) << value;
            return stream.str();
        }

        [[nodiscard]] Result<Float32> ReadFloat(const value& jsonValue)
        {
            if (jsonValue.is_double())
            {
                return Result<Float32>::Success(static_cast<Float32>(jsonValue.as_double()));
            }

            if (jsonValue.is_int64())
            {
                return Result<Float32>::Success(static_cast<Float32>(jsonValue.as_int64()));
            }

            if (jsonValue.is_uint64())
            {
                return Result<Float32>::Success(static_cast<Float32>(jsonValue.as_uint64()));
            }

            return Result<Float32>::Failure(Error(ErrorCode::InvalidArgument, "JSON value must be numeric."));
        }

        [[nodiscard]] Result<Vector3> ReadVector3(const value& jsonValue)
        {
            if (!jsonValue.is_array() || jsonValue.as_array().size() != 3)
            {
                return Result<Vector3>::Failure(Error(ErrorCode::InvalidArgument, "Expected a Vector3 JSON array."));
            }

            const array& values = jsonValue.as_array();
            Result<Float32> x = ReadFloat(values[0]);
            Result<Float32> y = ReadFloat(values[1]);
            Result<Float32> z = ReadFloat(values[2]);
            if (!x || !y || !z)
            {
                return Result<Vector3>::Failure(Error(ErrorCode::InvalidArgument, "Vector3 values must be numbers."));
            }

            return Result<Vector3>::Success(Vector3(x.GetValue(), y.GetValue(), z.GetValue()));
        }

        [[nodiscard]] Result<MeshVertex> ReadCompactMeshVertex(const value& jsonValue)
        {
            if (!jsonValue.is_array() || jsonValue.as_array().size() != 9)
            {
                return Result<MeshVertex>::Failure(
                    Error(ErrorCode::InvalidArgument, "Compact mesh vertex must contain 9 numbers."));
            }

            const array& values = jsonValue.as_array();
            MeshVertex vertex;

            Result<Float32> px = ReadFloat(values[0]);
            Result<Float32> py = ReadFloat(values[1]);
            Result<Float32> pz = ReadFloat(values[2]);
            Result<Float32> nx = ReadFloat(values[3]);
            Result<Float32> ny = ReadFloat(values[4]);
            Result<Float32> nz = ReadFloat(values[5]);
            Result<Float32> r = ReadFloat(values[6]);
            Result<Float32> g = ReadFloat(values[7]);
            Result<Float32> b = ReadFloat(values[8]);

            if (!px || !py || !pz || !nx || !ny || !nz || !r || !g || !b)
            {
                return Result<MeshVertex>::Failure(
                    Error(ErrorCode::InvalidArgument, "Compact mesh vertex values must be numbers."));
            }

            vertex.position = Vector3(px.GetValue(), py.GetValue(), pz.GetValue());
            vertex.normal = Vector3(nx.GetValue(), ny.GetValue(), nz.GetValue());
            vertex.color = Vector3(r.GetValue(), g.GetValue(), b.GetValue());
            return Result<MeshVertex>::Success(vertex);
        }

        void WriteCompactVertex(std::ostringstream& stream, const MeshVertex& vertex)
        {
            stream << '[' << FormatFloat(vertex.position.GetX()) << ", " << FormatFloat(vertex.position.GetY())
                   << ", " << FormatFloat(vertex.position.GetZ()) << ", " << FormatFloat(vertex.normal.GetX())
                   << ", " << FormatFloat(vertex.normal.GetY()) << ", " << FormatFloat(vertex.normal.GetZ()) << ", "
                   << FormatFloat(vertex.color.GetX()) << ", " << FormatFloat(vertex.color.GetY()) << ", "
                   << FormatFloat(vertex.color.GetZ()) << ']';
        }

        [[nodiscard]] std::string SerializeMeshAsset(const MeshAssetData& mesh)
        {
            std::ostringstream stream;
            stream << "{\n";
            stream << "  \"format\": \"VEngine.Mesh\",\n";
            stream << "  \"version\": 1,\n";
            stream << "  \"sourceGuid\": " << QuoteJsonString(mesh.sourceGuid.ToString()) << ",\n";
            stream << "  \"name\": " << QuoteJsonString(mesh.name) << ",\n";
            stream << "  \"vertexFormat\": [\"position3\", \"normal3\", \"color3\"],\n";
            stream << "  \"vertices\": [\n";

            for (SizeT index = 0; index < mesh.vertices.size(); ++index)
            {
                stream << "    ";
                WriteCompactVertex(stream, mesh.vertices[index]);
                stream << (index + 1 < mesh.vertices.size() ? ",\n" : "\n");
            }

            stream << "  ]\n";
            stream << "}\n";
            return stream.str();
        }
    } // namespace

    Result<MeshAssetData> LoadMeshAsset(const Path& path)
    {
        Result<object> jsonResult = ReadJsonObject(path);
        if (!jsonResult)
        {
            return Result<MeshAssetData>::Failure(jsonResult.GetError());
        }

        const object& root = jsonResult.GetValue();
        if (ReadString(root, "format") != "VEngine.Mesh")
        {
            return Result<MeshAssetData>::Failure(MakeInvalidJsonError(path, "Unsupported mesh asset format"));
        }

        const value* versionValue = FindMember(root, "version");
        if (versionValue == nullptr || !versionValue->is_int64() || versionValue->as_int64() != 1)
        {
            return Result<MeshAssetData>::Failure(MakeInvalidJsonError(path, "Unsupported mesh asset version"));
        }

        MeshAssetData mesh;
        mesh.name = ReadString(root, "name", "Mesh");

        Result<AssetGuid> sourceGuid = ReadGuid(root, "sourceGuid");
        if (sourceGuid)
        {
            mesh.sourceGuid = sourceGuid.GetValue();
        }

        if (!IsExpectedMeshVertexFormat(FindMember(root, "vertexFormat")))
        {
            return Result<MeshAssetData>::Failure(
                MakeInvalidJsonError(path, "Mesh asset vertexFormat must be [position3, normal3, color3]"));
        }

        const value* verticesValue = FindMember(root, "vertices");
        if (verticesValue == nullptr || !verticesValue->is_array())
        {
            return Result<MeshAssetData>::Failure(MakeInvalidJsonError(path, "Mesh asset missing vertices"));
        }

        for (const value& vertexValue : verticesValue->as_array())
        {
            Result<MeshVertex> vertex = ReadCompactMeshVertex(vertexValue);
            if (!vertex)
            {
                return Result<MeshAssetData>::Failure(vertex.GetError());
            }

            mesh.vertices.push_back(vertex.GetValue());
        }

        if (mesh.vertices.empty())
        {
            return Result<MeshAssetData>::Failure(MakeInvalidJsonError(path, "Mesh asset has no vertices"));
        }

        return Result<MeshAssetData>::Success(std::move(mesh));
    }

    ErrorCode SaveMeshAsset(const Path& path, const MeshAssetData& mesh)
    {
        return FileSystem::WriteTextFile(path, SerializeMeshAsset(mesh));
    }

    Result<MaterialAssetData> LoadMaterialAsset(const Path& path)
    {
        Result<object> jsonResult = ReadJsonObject(path);
        if (!jsonResult)
        {
            return Result<MaterialAssetData>::Failure(jsonResult.GetError());
        }

        const object& root = jsonResult.GetValue();
        if (ReadString(root, "format") != "VEngine.Material")
        {
            return Result<MaterialAssetData>::Failure(MakeInvalidJsonError(path, "Unsupported material format"));
        }

        const value* versionValue = FindMember(root, "version");
        if (versionValue == nullptr || !versionValue->is_int64() || versionValue->as_int64() != 1)
        {
            return Result<MaterialAssetData>::Failure(MakeInvalidJsonError(path, "Unsupported material version"));
        }

        MaterialAssetData material;
        material.name = ReadString(root, "name", "Material");

        Result<AssetGuid> guid = ReadGuid(root, "guid");
        if (!guid)
        {
            return Result<MaterialAssetData>::Failure(guid.GetError());
        }

        material.guid = guid.GetValue();

        if (const value* parametersValue = FindMember(root, "parameters");
            parametersValue != nullptr && parametersValue->is_object())
        {
            const object& parameters = parametersValue->as_object();
            if (const value* baseColorValue = FindMember(parameters, "baseColor"); baseColorValue != nullptr)
            {
                Result<Vector3> baseColor = ReadVector3(*baseColorValue);
                if (!baseColor)
                {
                    return Result<MaterialAssetData>::Failure(baseColor.GetError());
                }

                material.baseColor = baseColor.GetValue();
            }
        }

        return Result<MaterialAssetData>::Success(material);
    }

    ErrorCode SaveMaterialAsset(const Path& path, const MaterialAssetData& material)
    {
        object root;
        root["format"] = "VEngine.Material";
        root["version"] = 1;
        root["guid"] = material.guid.ToString();
        root["name"] = material.name;

        object parameters;
        parameters["baseColor"] = ToJson(material.baseColor);
        root["parameters"] = std::move(parameters);
        root["textures"] = array();

        return FileSystem::WriteTextFile(path, boost::json::serialize(root));
    }
} // namespace ve
