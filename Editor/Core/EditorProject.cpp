#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Asset/SceneAssetLoader.h"
#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/GameThread/GameThreadSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/Serialization/SceneSerialization.h"
#include "Engine/Runtime/Scripting/ScriptContext.h"
#include "Engine/Runtime/Scripting/ScriptHost.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <utility>

#ifndef VE_ENGINE_SOURCE_DIR
#define VE_ENGINE_SOURCE_DIR ""
#endif

#ifndef VE_DOTNET_TARGET_FRAMEWORK
#define VE_DOTNET_TARGET_FRAMEWORK "net10.0"
#endif

namespace ve
{
    namespace
    {
        using boost::json::array;
        using boost::json::object;
        using boost::json::value;

        constexpr std::string_view ProjectDescriptorFileName = ".veproject";
        constexpr std::string_view ProjectFormatName = "VEngine.Project";

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

        [[nodiscard]] bool IsProjectRelativeAuthoredAssetPath(const Path& path) noexcept
        {
            const std::string& text = path.GetString();
            return !path.IsAbsolute() && (text == "Assets" || text.starts_with("Assets/"));
        }

        void WriteIndent(std::ostringstream& stream, SizeT indent)
        {
            for (SizeT index = 0; index < indent; ++index)
            {
                stream << ' ';
            }
        }

        [[nodiscard]] std::string QuoteJsonString(std::string_view text)
        {
            return boost::json::serialize(value(boost::json::string(text)));
        }

        [[nodiscard]] std::string FormatSceneJsonDouble(double number)
        {
            if (!std::isfinite(number))
            {
                return boost::json::serialize(value(number));
            }

            if (std::fabs(number) < 0.0000005)
            {
                number = 0.0;
            }

            const double absoluteNumber = std::fabs(number);
            std::ostringstream stream;
            if (absoluteNumber >= 10000000.0 || (absoluteNumber > 0.0 && absoluteNumber < 0.000001))
            {
                stream << std::defaultfloat << std::setprecision(7) << number;
                return stream.str();
            }

            stream << std::fixed << std::setprecision(6) << number;
            std::string text = stream.str();
            const std::string::size_type decimalPosition = text.find('.');
            if (decimalPosition == std::string::npos)
            {
                text += ".0";
                return text;
            }

            while (text.size() > decimalPosition + 2 && text.back() == '0')
            {
                text.pop_back();
            }

            if (text.back() == '.')
            {
                text += '0';
            }

            return text;
        }

        void WritePrettyJsonValue(std::ostringstream& stream, const value& jsonValue, SizeT indent);

        void WritePrettyJsonObject(std::ostringstream& stream, const object& jsonObject, SizeT indent)
        {
            if (jsonObject.empty())
            {
                stream << "{}";
                return;
            }

            stream << "{\n";
            SizeT index = 0;
            for (const auto& member : jsonObject)
            {
                WriteIndent(stream, indent + 2);
                stream << QuoteJsonString(std::string_view(member.key().data(), member.key().size())) << ": ";
                WritePrettyJsonValue(stream, member.value(), indent + 2);
                stream << (++index < jsonObject.size() ? ",\n" : "\n");
            }

            WriteIndent(stream, indent);
            stream << '}';
        }

        [[nodiscard]] bool CanWriteJsonArrayInline(const array& jsonArray) noexcept
        {
            for (const value& item : jsonArray)
            {
                if (item.is_object() || item.is_array())
                {
                    return false;
                }
            }

            return true;
        }

        void WritePrettyJsonArray(std::ostringstream& stream, const array& jsonArray, SizeT indent)
        {
            if (jsonArray.empty())
            {
                stream << "[]";
                return;
            }

            if (CanWriteJsonArrayInline(jsonArray))
            {
                stream << '[';
                for (SizeT index = 0; index < jsonArray.size(); ++index)
                {
                    WritePrettyJsonValue(stream, jsonArray[index], indent);
                    if (index + 1 < jsonArray.size())
                    {
                        stream << ", ";
                    }
                }

                stream << ']';
                return;
            }

            stream << "[\n";
            for (SizeT index = 0; index < jsonArray.size(); ++index)
            {
                WriteIndent(stream, indent + 2);
                WritePrettyJsonValue(stream, jsonArray[index], indent + 2);
                stream << (index + 1 < jsonArray.size() ? ",\n" : "\n");
            }

            WriteIndent(stream, indent);
            stream << ']';
        }

        void WritePrettyJsonValue(std::ostringstream& stream, const value& jsonValue, SizeT indent)
        {
            if (jsonValue.is_object())
            {
                WritePrettyJsonObject(stream, jsonValue.as_object(), indent);
                return;
            }

            if (jsonValue.is_array())
            {
                WritePrettyJsonArray(stream, jsonValue.as_array(), indent);
                return;
            }

            if (jsonValue.is_double())
            {
                stream << FormatSceneJsonDouble(jsonValue.as_double());
                return;
            }

            stream << boost::json::serialize(jsonValue);
        }

        [[nodiscard]] std::string SerializePrettyJsonObject(const object& jsonObject)
        {
            std::ostringstream stream;
            WritePrettyJsonObject(stream, jsonObject, 0);
            stream << '\n';
            return stream.str();
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
                    Error(ErrorCode::InvalidArgument, "Project descriptor root must be a JSON object."));
            }

            return Result<object>::Success(std::move(root.as_object()));
        }

        [[nodiscard]] Result<AssetGuid> ReadGuid(const object& jsonObject, const char* name)
        {
            const value* member = FindMember(jsonObject, name);
            if (member == nullptr || !member->is_string())
            {
                return Result<AssetGuid>::Failure(
                    Error(ErrorCode::InvalidArgument, "Project descriptor is missing a GUID field."));
            }

            const boost::json::string& guidText = member->as_string();
            return AssetGuid::Parse(std::string_view(guidText.data(), guidText.size()));
        }

        [[nodiscard]] std::vector<std::string> ReadStringArray(const object& jsonObject, const char* name)
        {
            std::vector<std::string> values;
            const value* arrayValue = FindMember(jsonObject, name);
            if (arrayValue == nullptr || !arrayValue->is_array())
            {
                return values;
            }

            for (const value& item : arrayValue->as_array())
            {
                if (item.is_string())
                {
                    values.emplace_back(item.as_string());
                }
            }

            return values;
        }

        [[nodiscard]] ErrorCode EnsureDirectory(const Path& path)
        {
            if (FileSystem::Exists(path) && !FileSystem::IsDirectory(path))
            {
                return ErrorCode::InvalidArgument;
            }

            return FileSystem::CreateDirectories(path);
        }

        [[nodiscard]] std::vector<Path> GetGeneratedDirectories(const Path& projectRoot)
        {
            return {
                projectRoot / "Generated",
                projectRoot / "Generated/Assets",
                projectRoot / "Generated/Assets/ImportCache",
                projectRoot / "Generated/Shaders",
                projectRoot / "Generated/Shaders/Windows",
                projectRoot / "Generated/Shaders/Windows/D3D11",
                projectRoot / "Generated/Shaders/Windows/D3D12",
                projectRoot / "Generated/Shaders/iOS",
                projectRoot / "Generated/Shaders/iOS/Metal",
                projectRoot / "Generated/Scripts",
                projectRoot / "Generated/Scripts/Windows",
                projectRoot / "Generated/Scripts/Windows/Debug",
                projectRoot / "Generated/Scripts/Windows/Release",
                projectRoot / "Generated/Intermediates",
                projectRoot / "Generated/Intermediates/Scripts",
                projectRoot / "Generated/Intermediates/Scripts/Windows",
                projectRoot / "Generated/Build",
                projectRoot / "Generated/Build/Windows",
                projectRoot / "Generated/Build/iOS",
                projectRoot / "Generated/Editor",
                projectRoot / "Generated/Editor/Workspace",
                projectRoot / "Generated/Editor/Thumbnails",
                projectRoot / "Generated/Logs",
                projectRoot / "Generated/Temp",
            };
        }

        [[nodiscard]] object WriteSceneReference(const EditorProjectSceneReference& reference)
        {
            object sceneJson;
            if (reference.guid.IsValid())
            {
                sceneJson["guid"] = reference.guid.ToString();
            }

            sceneJson["path"] = reference.path.GetString();
            return sceneJson;
        }

        [[nodiscard]] object WriteProjectDescriptor(const EditorProjectDescriptor& descriptor)
        {
            object root;
            root["format"] = std::string(ProjectFormatName);
            root["version"] = descriptor.version;
            root["guid"] = descriptor.guid.ToString();
            root["name"] = descriptor.displayName;
            root["engineVersion"] = descriptor.engineVersion;
            root["startupScene"] = WriteSceneReference(descriptor.startupScene);

            array platforms;
            for (const std::string& platform : descriptor.targetPlatforms)
            {
                platforms.push_back(boost::json::string(platform));
            }
            root["targetPlatforms"] = std::move(platforms);

            return root;
        }

        [[nodiscard]] std::string EscapeXmlAttribute(std::string_view text)
        {
            std::string escaped;
            for (const char value : text)
            {
                switch (value)
                {
                case '&':
                    escaped += "&amp;";
                    break;
                case '"':
                    escaped += "&quot;";
                    break;
                case '\'':
                    escaped += "&apos;";
                    break;
                case '<':
                    escaped += "&lt;";
                    break;
                case '>':
                    escaped += "&gt;";
                    break;
                default:
                    escaped.push_back(value);
                    break;
                }
            }

            return escaped;
        }

        [[nodiscard]] Path GetScriptApiProjectPath()
        {
            return Path(VE_ENGINE_SOURCE_DIR) / "Managed/VEngine.ScriptAPI/VEngine.ScriptAPI.csproj";
        }

        [[nodiscard]] std::string ToSolutionPath(std::string text)
        {
            std::replace(text.begin(), text.end(), '/', '\\');
            return text;
        }

        [[nodiscard]] std::string WriteWindowsScriptProjectFile()
        {
            std::ostringstream stream;
            stream << "<Project Sdk=\"Microsoft.NET.Sdk\">\n";
            stream << "  <PropertyGroup>\n";
            stream << "    <TargetFramework Condition=\"'$(TargetFramework)' == ''\">"
                   << VE_DOTNET_TARGET_FRAMEWORK << "</TargetFramework>\n";
            stream << "    <AssemblyName>" << GetWindowsScriptAssemblyName() << "</AssemblyName>\n";
            stream << "    <RootNamespace>" << GetWindowsScriptAssemblyName() << "</RootNamespace>\n";
            stream << "    <ImplicitUsings>disable</ImplicitUsings>\n";
            stream << "    <LangVersion>latest</LangVersion>\n";
            stream << "    <Nullable>enable</Nullable>\n";
            stream << "    <OutputType>Library</OutputType>\n";
            stream << "    <EnableDynamicLoading>true</EnableDynamicLoading>\n";
            stream << "    <GenerateRuntimeConfigurationFiles>true</GenerateRuntimeConfigurationFiles>\n";
            stream << "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n";
            stream << "    <DefaultItemExcludes>$(DefaultItemExcludes);bin/**;obj/**</DefaultItemExcludes>\n";
            stream << "  </PropertyGroup>\n\n";
            stream << "  <ItemGroup>\n";
            stream << "    <Compile Include=\"../../../" << GetWindowsScriptSourceRelativeDirectory().GetString()
                   << "/**/*.cs\" Link=\"Scripts/%(RecursiveDir)%(Filename)%(Extension)\" />\n";
            stream << "    <ProjectReference Include=\""
                   << EscapeXmlAttribute(GetScriptApiProjectPath().GetString()) << "\" />\n";
            stream << "  </ItemGroup>\n";
            stream << "</Project>\n";
            return stream.str();
        }

        [[nodiscard]] std::string WriteWindowsScriptSolutionFile()
        {
            constexpr std::string_view csharpProjectTypeGuid = "{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}";
            constexpr std::string_view scriptProjectGuid = "{4D779A8E-A1F8-4D8E-A1A3-F1F4A10A8E01}";
            constexpr std::string_view scriptApiProjectGuid = "{16AE4D74-21E2-41C4-8B28-E0824C1C7F41}";

            std::ostringstream stream;
            stream << "Microsoft Visual Studio Solution File, Format Version 12.00\n";
            stream << "# Visual Studio Version 17\n";
            stream << "VisualStudioVersion = 17.0.31903.59\n";
            stream << "MinimumVisualStudioVersion = 10.0.40219.1\n";
            stream << "Project(\"" << csharpProjectTypeGuid << "\") = \"" << GetWindowsScriptAssemblyName()
                   << "\", \"VE.Scripting.csproj\", \"" << scriptProjectGuid << "\"\n";
            stream << "EndProject\n";
            stream << "Project(\"" << csharpProjectTypeGuid << "\") = \"VEngine.ScriptAPI\", \""
                   << ToSolutionPath(GetScriptApiProjectPath().GetString()) << "\", \"" << scriptApiProjectGuid
                   << "\"\n";
            stream << "EndProject\n";
            stream << "Global\n";
            stream << "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\n";
            stream << "\t\tDebug|Any CPU = Debug|Any CPU\n";
            stream << "\t\tRelease|Any CPU = Release|Any CPU\n";
            stream << "\tEndGlobalSection\n";
            stream << "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\n";
            stream << "\t\t" << scriptProjectGuid << ".Debug|Any CPU.ActiveCfg = Debug|Any CPU\n";
            stream << "\t\t" << scriptProjectGuid << ".Debug|Any CPU.Build.0 = Debug|Any CPU\n";
            stream << "\t\t" << scriptProjectGuid << ".Release|Any CPU.ActiveCfg = Release|Any CPU\n";
            stream << "\t\t" << scriptProjectGuid << ".Release|Any CPU.Build.0 = Release|Any CPU\n";
            stream << "\t\t" << scriptApiProjectGuid << ".Debug|Any CPU.ActiveCfg = Debug|Any CPU\n";
            stream << "\t\t" << scriptApiProjectGuid << ".Debug|Any CPU.Build.0 = Debug|Any CPU\n";
            stream << "\t\t" << scriptApiProjectGuid << ".Release|Any CPU.ActiveCfg = Release|Any CPU\n";
            stream << "\t\t" << scriptApiProjectGuid << ".Release|Any CPU.Build.0 = Release|Any CPU\n";
            stream << "\tEndGlobalSection\n";
            stream << "EndGlobal\n";
            return stream.str();
        }

        [[nodiscard]] ErrorCode EnsureWindowsScriptWorkspace(const Path& projectRoot)
        {
            const Path sourceDirectory = GetWindowsScriptSourceDirectory(projectRoot);
            ErrorCode sourceDirectoryResult = EnsureDirectory(sourceDirectory);
            if (sourceDirectoryResult != ErrorCode::None)
            {
                return sourceDirectoryResult;
            }

            const Path scriptProjectPath = GetWindowsScriptProjectPath(projectRoot);
            ErrorCode workspaceDirectoryResult = EnsureDirectory(scriptProjectPath.GetParentPath());
            if (workspaceDirectoryResult != ErrorCode::None)
            {
                return workspaceDirectoryResult;
            }

            ErrorCode projectResult = FileSystem::WriteTextFile(scriptProjectPath, WriteWindowsScriptProjectFile());
            if (projectResult != ErrorCode::None)
            {
                return projectResult;
            }

            return FileSystem::WriteTextFile(GetWindowsScriptSolutionPath(projectRoot),
                                             WriteWindowsScriptSolutionFile());
        }

        [[nodiscard]] object WriteEmptySceneAsset(const AssetGuid& sceneGuid, std::string_view name)
        {
            object sceneData;
            sceneData["gameObjects"] = array();

            object root;
            root["format"] = "VEngine.Scene";
            root["version"] = 1;
            root["guid"] = sceneGuid.ToString();
            root["name"] = std::string(name);
            root["scene"] = std::move(sceneData);
            return root;
        }

        [[nodiscard]] std::string MakeSceneNameFromPath(const Path& scenePath)
        {
            std::string filename = scenePath.GetFilename();
            const SizeT dot = filename.find_last_of('.');
            if (dot != std::string::npos)
            {
                filename.erase(dot);
            }

            return filename.empty() ? "Scene" : filename;
        }

        [[nodiscard]] UInt64 ReadUInt64Value(const value* jsonValue, UInt64 fallback = 0) noexcept
        {
            if (jsonValue == nullptr)
            {
                return fallback;
            }

            if (jsonValue->is_uint64())
            {
                return jsonValue->as_uint64();
            }

            if (jsonValue->is_int64() && jsonValue->as_int64() >= 0)
            {
                return static_cast<UInt64>(jsonValue->as_int64());
            }

            return fallback;
        }

        [[nodiscard]] const array* FindSceneGameObjects(const object& sceneAsset)
        {
            const value* sceneValue = FindMember(sceneAsset, "scene");
            if (sceneValue == nullptr || !sceneValue->is_object())
            {
                return nullptr;
            }

            const value* gameObjectsValue = FindMember(sceneValue->as_object(), "gameObjects");
            return gameObjectsValue != nullptr && gameObjectsValue->is_array() ? &gameObjectsValue->as_array()
                                                                               : nullptr;
        }

        [[nodiscard]] array* FindSceneGameObjects(object& sceneAsset)
        {
            value* sceneValue = sceneAsset.if_contains("scene");
            if (sceneValue == nullptr || !sceneValue->is_object())
            {
                return nullptr;
            }

            value* gameObjectsValue = sceneValue->as_object().if_contains("gameObjects");
            return gameObjectsValue != nullptr && gameObjectsValue->is_array() ? &gameObjectsValue->as_array()
                                                                               : nullptr;
        }

        [[nodiscard]] const object* FindGameObjectById(const array& gameObjects, UInt64 id)
        {
            for (const value& gameObjectValue : gameObjects)
            {
                if (!gameObjectValue.is_object())
                {
                    continue;
                }

                const object& gameObjectJson = gameObjectValue.as_object();
                if (ReadUInt64Value(FindMember(gameObjectJson, "id")) == id)
                {
                    return &gameObjectJson;
                }
            }

            return nullptr;
        }

        [[nodiscard]] object* FindGameObjectById(array& gameObjects, UInt64 id)
        {
            for (value& gameObjectValue : gameObjects)
            {
                if (!gameObjectValue.is_object())
                {
                    continue;
                }

                object& gameObjectJson = gameObjectValue.as_object();
                if (ReadUInt64Value(FindMember(gameObjectJson, "id")) == id)
                {
                    return &gameObjectJson;
                }
            }

            return nullptr;
        }

        [[nodiscard]] bool IsAssetReferenceValue(const value& jsonValue) noexcept
        {
            return jsonValue.is_object() || jsonValue.is_string();
        }

        [[nodiscard]] AssetGuid ReadOptionalAssetGuid(const object& jsonObject, const char* name)
        {
            const value* member = FindMember(jsonObject, name);
            if (member == nullptr || !member->is_string())
            {
                return {};
            }

            const boost::json::string& guidText = member->as_string();
            Result<AssetGuid> guid = AssetGuid::Parse(std::string_view(guidText.data(), guidText.size()));
            return guid ? guid.GetValue() : AssetGuid();
        }

        [[nodiscard]] EditorAuthoredAssetReference ReadAuthoredAssetReference(const value& jsonValue)
        {
            EditorAuthoredAssetReference reference;
            if (jsonValue.is_object())
            {
                const object& referenceJson = jsonValue.as_object();
                reference.guid = ReadOptionalAssetGuid(referenceJson, "guid");
                reference.path = Path(ReadString(referenceJson, "path"));
            }
            else if (jsonValue.is_string())
            {
                reference.path = Path(std::string(jsonValue.as_string()));
            }

            reference.hasValue = reference.guid.IsValid() || !reference.path.IsEmpty();
            return reference;
        }

        [[nodiscard]] value WriteAuthoredAssetReference(const EditorAuthoredAssetReference& reference)
        {
            object referenceJson;
            if (reference.guid.IsValid())
            {
                referenceJson["guid"] = reference.guid.ToString();
            }

            if (!reference.path.IsEmpty())
            {
                referenceJson["path"] = reference.path.GetString();
            }

            return value(std::move(referenceJson));
        }

        [[nodiscard]] std::vector<EditorMeshRendererAssetReferences>
        ExtractMeshRendererAssetReferences(const object& sceneAsset)
        {
            std::vector<EditorMeshRendererAssetReferences> references;
            const array* gameObjects = FindSceneGameObjects(sceneAsset);
            if (gameObjects == nullptr)
            {
                return references;
            }

            for (const value& gameObjectValue : *gameObjects)
            {
                if (!gameObjectValue.is_object())
                {
                    continue;
                }

                const object& gameObjectJson = gameObjectValue.as_object();
                const SceneObjectId gameObjectId = ReadUInt64Value(FindMember(gameObjectJson, "id"));
                if (gameObjectId == InvalidSceneObjectId)
                {
                    continue;
                }

                const value* componentsValue = FindMember(gameObjectJson, "components");
                if (componentsValue == nullptr || !componentsValue->is_array())
                {
                    continue;
                }

                const array& components = componentsValue->as_array();
                for (SizeT componentIndex = 0; componentIndex < components.size(); ++componentIndex)
                {
                    if (!components[componentIndex].is_object())
                    {
                        continue;
                    }

                    const object& componentJson = components[componentIndex].as_object();
                    if (ReadString(componentJson, "type") != "MeshRendererComponent")
                    {
                        continue;
                    }

                    const value* propertiesValue = FindMember(componentJson, "properties");
                    if (propertiesValue == nullptr || !propertiesValue->is_object())
                    {
                        continue;
                    }

                    const object& properties = propertiesValue->as_object();
                    EditorMeshRendererAssetReferences entry;
                    entry.gameObjectId = gameObjectId;
                    entry.componentIndex = componentIndex;

                    if (const value* meshValue = FindMember(properties, "mesh");
                        meshValue != nullptr && IsAssetReferenceValue(*meshValue))
                    {
                        entry.mesh = ReadAuthoredAssetReference(*meshValue);
                    }

                    if (const value* materialValue = FindMember(properties, "material");
                        materialValue != nullptr && IsAssetReferenceValue(*materialValue))
                    {
                        entry.material = ReadAuthoredAssetReference(*materialValue);
                    }

                    if (entry.mesh.IsSet() || entry.material.IsSet())
                    {
                        references.push_back(std::move(entry));
                    }
                }
            }

            return references;
        }

        void ApplyMeshRendererAssetReferences(
            object& sceneAsset,
            const std::vector<EditorMeshRendererAssetReferences>& references)
        {
            array* gameObjects = FindSceneGameObjects(sceneAsset);
            if (gameObjects == nullptr)
            {
                return;
            }

            for (const EditorMeshRendererAssetReferences& reference : references)
            {
                object* gameObject = FindGameObjectById(*gameObjects, reference.gameObjectId);
                if (gameObject == nullptr)
                {
                    continue;
                }

                value* componentsValue = gameObject->if_contains("components");
                if (componentsValue == nullptr || !componentsValue->is_array() ||
                    reference.componentIndex >= componentsValue->as_array().size())
                {
                    continue;
                }

                value& componentValue = componentsValue->as_array()[reference.componentIndex];
                if (!componentValue.is_object())
                {
                    continue;
                }

                object& component = componentValue.as_object();
                if (ReadString(component, "type") != "MeshRendererComponent")
                {
                    continue;
                }

                value* propertiesValue = component.if_contains("properties");
                if (propertiesValue == nullptr || !propertiesValue->is_object())
                {
                    component["properties"] = object();
                    propertiesValue = component.if_contains("properties");
                }

                if (propertiesValue == nullptr || !propertiesValue->is_object())
                {
                    continue;
                }

                object& properties = propertiesValue->as_object();
                if (reference.mesh.IsSet())
                {
                    properties["mesh"] = WriteAuthoredAssetReference(reference.mesh);
                }

                if (reference.material.IsSet())
                {
                    properties["material"] = WriteAuthoredAssetReference(reference.material);
                }
            }
        }

        void PreserveReferenceProperty(object& nextProperties, const object& previousProperties, const char* name)
        {
            value* nextValue = nextProperties.if_contains(name);
            const value* previousValue = FindMember(previousProperties, name);
            if (nextValue != nullptr && previousValue != nullptr && IsAssetReferenceValue(*previousValue))
            {
                *nextValue = *previousValue;
            }
        }

        void PreserveMeshRendererReferences(object& nextComponent, const object& previousComponent)
        {
            value* nextPropertiesValue = nextComponent.if_contains("properties");
            const value* previousPropertiesValue = FindMember(previousComponent, "properties");
            if (nextPropertiesValue == nullptr || previousPropertiesValue == nullptr ||
                !nextPropertiesValue->is_object() || !previousPropertiesValue->is_object())
            {
                return;
            }

            object& nextProperties = nextPropertiesValue->as_object();
            const object& previousProperties = previousPropertiesValue->as_object();
            PreserveReferenceProperty(nextProperties, previousProperties, "mesh");
            PreserveReferenceProperty(nextProperties, previousProperties, "material");
        }

        void PreserveSceneAssetReferences(object& nextSceneAsset, const object& previousSceneAsset)
        {
            array* nextGameObjects = FindSceneGameObjects(nextSceneAsset);
            const array* previousGameObjects = FindSceneGameObjects(previousSceneAsset);
            if (nextGameObjects == nullptr || previousGameObjects == nullptr)
            {
                return;
            }

            for (value& nextGameObjectValue : *nextGameObjects)
            {
                if (!nextGameObjectValue.is_object())
                {
                    continue;
                }

                object& nextGameObject = nextGameObjectValue.as_object();
                const UInt64 gameObjectId = ReadUInt64Value(FindMember(nextGameObject, "id"));
                const object* previousGameObject = FindGameObjectById(*previousGameObjects, gameObjectId);
                if (previousGameObject == nullptr)
                {
                    continue;
                }

                value* nextComponentsValue = nextGameObject.if_contains("components");
                const value* previousComponentsValue = FindMember(*previousGameObject, "components");
                if (nextComponentsValue == nullptr || previousComponentsValue == nullptr ||
                    !nextComponentsValue->is_array() || !previousComponentsValue->is_array())
                {
                    continue;
                }

                const array& previousComponents = previousComponentsValue->as_array();
                for (SizeT componentIndex = 0; componentIndex < nextComponentsValue->as_array().size();
                     ++componentIndex)
                {
                    value& nextComponentValue = nextComponentsValue->as_array()[componentIndex];
                    if (!nextComponentValue.is_object() || componentIndex >= previousComponents.size() ||
                        !previousComponents[componentIndex].is_object())
                    {
                        continue;
                    }

                    object& nextComponent = nextComponentValue.as_object();
                    const object& previousComponent = previousComponents[componentIndex].as_object();
                    if (ReadString(nextComponent, "type") == "MeshRendererComponent" &&
                        ReadString(previousComponent, "type") == "MeshRendererComponent")
                    {
                        PreserveMeshRendererReferences(nextComponent, previousComponent);
                    }
                }
            }
        }

        [[nodiscard]] Result<object> MakeSceneAssetJson(const Scene& scene,
                                                        const ReflectionRegistry& reflectionRegistry,
                                                        const AssetGuid& sceneGuid,
                                                        const Path& scenePath)
        {
            boost::system::error_code parseError;
            value serializedScene = boost::json::parse(SerializeSceneToJson(scene, reflectionRegistry), parseError);
            if (parseError || !serializedScene.is_object())
            {
                return Result<object>::Failure(
                    Error(ErrorCode::InvalidState, "Scene serialization did not produce a JSON object."));
            }

            object sceneBody;
            const object& serializedRoot = serializedScene.as_object();
            if (const value* gameObjects = FindMember(serializedRoot, "gameObjects");
                gameObjects != nullptr && gameObjects->is_array())
            {
                sceneBody["gameObjects"] = *gameObjects;
            }
            else
            {
                sceneBody["gameObjects"] = array();
            }

            object root;
            root["format"] = "VEngine.Scene";
            root["version"] = 1;
            root["guid"] = sceneGuid.ToString();
            root["name"] = MakeSceneNameFromPath(scenePath);
            root["scene"] = std::move(sceneBody);
            return Result<object>::Success(std::move(root));
        }

        [[nodiscard]] std::string MakeProjectNameFromPath(const Path& projectRoot)
        {
            std::string filename = projectRoot.GetFilename();
            return filename.empty() ? "VEngineProject" : filename;
        }

        void LogDiagnostic(const EditorProjectDiagnostic& diagnostic)
        {
            switch (diagnostic.severity)
            {
            case EditorProjectDiagnosticSeverity::Info:
                VE_LOG_INFO_CATEGORY("EditorProject", "{}", diagnostic.message);
                break;
            case EditorProjectDiagnosticSeverity::Warning:
                VE_LOG_WARN_CATEGORY("EditorProject", "{}", diagnostic.message);
                break;
            case EditorProjectDiagnosticSeverity::Error:
                VE_LOG_ERROR_CATEGORY("EditorProject", "{}", diagnostic.message);
                break;
            }
        }
    } // namespace

    EditorProjectService::EditorProjectService() = default;

    EditorProjectService::~EditorProjectService() = default;

    void EditorProjectAssetService::Clear() noexcept
    {
        assetDatabase_ = {};
    }

    ErrorCode EditorProjectAssetService::Open(const Path& projectRoot,
                                              const EditorProjectDiagnosticSink& diagnostics)
    {
        const ErrorCode openResult = assetDatabase_.Open(projectRoot);
        if (openResult != ErrorCode::None)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        std::string("AssetDatabase open failed: ") + ToString(openResult));
            Clear();
            return openResult;
        }

        diagnostics(EditorProjectDiagnosticSeverity::Info,
                    "AssetDatabase refreshed " + std::to_string(assetDatabase_.GetRecords().size()) + " asset(s).");
        return ErrorCode::None;
    }

    ErrorCode EditorProjectAssetService::Refresh(const EditorProjectDiagnosticSink& diagnostics)
    {
        const ErrorCode refreshResult = assetDatabase_.Refresh();
        if (refreshResult != ErrorCode::None)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        std::string("AssetDatabase refresh failed: ") + ToString(refreshResult));
            return refreshResult;
        }

        diagnostics(EditorProjectDiagnosticSeverity::Info,
                    "AssetDatabase refreshed " + std::to_string(assetDatabase_.GetRecords().size()) + " asset(s).");
        return ErrorCode::None;
    }

    ErrorCode EditorProjectAssetService::Validate(const EditorProjectDiagnosticSink& diagnostics) const
    {
        const ErrorCode validationResult = assetDatabase_.Validate();
        if (validationResult != ErrorCode::None)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Warning,
                        "AssetDatabase validation reported missing generated artifacts or source files.");
        }

        return validationResult;
    }

    AssetDatabase& EditorProjectAssetService::GetDatabase() noexcept
    {
        return assetDatabase_;
    }

    const AssetDatabase& EditorProjectAssetService::GetDatabase() const noexcept
    {
        return assetDatabase_;
    }

    void EditorProjectSceneService::Clear() noexcept
    {
        currentEditScene_.Clear();
        currentScenePath_ = {};
        currentSceneGuid_ = {};
        meshRendererAssetReferences_.clear();
        dirty_ = false;
    }

    void EditorProjectSceneService::OpenEmptyEditScene() noexcept
    {
        currentEditScene_.Clear();
        currentEditScene_.UpdateTransforms();
        currentScenePath_ = {};
        currentSceneGuid_ = {};
        meshRendererAssetReferences_.clear();
        dirty_ = false;
    }

    ErrorCode EditorProjectSceneService::OpenScene(const Path& projectRelativeScenePath,
                                                   AssetDatabase& assetDatabase,
                                                   ResourceManager& resourceManager,
                                                   const EditorProjectDiagnosticSink& diagnostics)
    {
        if (projectRelativeScenePath.IsEmpty())
        {
            return ErrorCode::InvalidState;
        }

        const AssetRecord* record = assetDatabase.FindAssetByPath(projectRelativeScenePath);
        if (record == nullptr)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "Scene asset was not found: " + projectRelativeScenePath.GetString());
            return ErrorCode::NotFound;
        }

        if (record->assetType != AssetType::Scene)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "Asset is not a scene: " + projectRelativeScenePath.GetString());
            return ErrorCode::InvalidArgument;
        }

        return OpenSceneFromRecord(*record, assetDatabase, resourceManager, diagnostics);
    }

    ErrorCode EditorProjectSceneService::OpenSceneFromRecord(const AssetRecord& record,
                                                             AssetDatabase& assetDatabase,
                                                             ResourceManager& resourceManager,
                                                             const EditorProjectDiagnosticSink& diagnostics)
    {
        if (record.assetType != AssetType::Scene || !record.guid.IsValid() || record.path.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const Path scenePath = assetDatabase.ResolveProjectPath(record.path);
        if (!FileSystem::IsFile(scenePath))
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error, "Scene file is missing: " + record.path.GetString());
            return ErrorCode::NotFound;
        }

        ReflectionRegistry reflectionRegistry;
        RegisterSceneReflectionTypes(reflectionRegistry);

        Result<object> authoredSceneAsset = ReadJsonObject(scenePath);
        if (!authoredSceneAsset)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error, "Scene parse failed: " + record.path.GetString());
            return authoredSceneAsset.GetError().GetCode();
        }

        const ErrorCode sceneResult =
            LoadSceneAsset(currentEditScene_, reflectionRegistry, resourceManager, assetDatabase, scenePath);
        if (sceneResult != ErrorCode::None)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "Scene load failed: " + record.path.GetString() + " (" + ToString(sceneResult) + ")");
            return sceneResult;
        }

        currentScenePath_ = record.path;
        currentSceneGuid_ = record.guid;
        meshRendererAssetReferences_ = ExtractMeshRendererAssetReferences(authoredSceneAsset.GetValue());
        dirty_ = false;
        diagnostics(EditorProjectDiagnosticSeverity::Info, "Opened scene: " + currentScenePath_.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectSceneService::SaveCurrentScene(AssetDatabase& assetDatabase,
                                                          const EditorProjectDiagnosticSink& diagnostics)
    {
        if (currentScenePath_.IsEmpty() || !currentSceneGuid_.IsValid())
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "Current edit scene has no authored .vescene asset to save.");
            return ErrorCode::InvalidState;
        }

        ReflectionRegistry reflectionRegistry;
        RegisterSceneReflectionTypes(reflectionRegistry);

        Result<object> sceneAsset =
            MakeSceneAssetJson(currentEditScene_, reflectionRegistry, currentSceneGuid_, currentScenePath_);
        if (!sceneAsset)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error, sceneAsset.GetError().GetMessage());
            return sceneAsset.GetError().GetCode();
        }

        const Path scenePath = assetDatabase.ResolveProjectPath(currentScenePath_);
        if (Result<object> previousSceneAsset = ReadJsonObject(scenePath))
        {
            PreserveSceneAssetReferences(sceneAsset.GetValue(), previousSceneAsset.GetValue());
        }

        ApplyMeshRendererAssetReferences(sceneAsset.GetValue(), meshRendererAssetReferences_);

        const ErrorCode writeResult =
            FileSystem::WriteTextFile(scenePath, SerializePrettyJsonObject(sceneAsset.GetValue()));
        if (writeResult != ErrorCode::None)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error, "Failed to save scene: " + currentScenePath_.GetString());
            return writeResult;
        }

        meshRendererAssetReferences_ = ExtractMeshRendererAssetReferences(sceneAsset.GetValue());
        dirty_ = false;
        const ErrorCode refreshResult = assetDatabase.Refresh();
        if (refreshResult != ErrorCode::None)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Warning,
                        "Scene saved, but AssetDatabase refresh failed: " + std::string(ToString(refreshResult)));
            return refreshResult;
        }

        diagnostics(EditorProjectDiagnosticSeverity::Info, "Saved scene: " + currentScenePath_.GetString());
        return ErrorCode::None;
    }

    Scene& EditorProjectSceneService::GetCurrentEditScene() noexcept
    {
        return currentEditScene_;
    }

    const Scene& EditorProjectSceneService::GetCurrentEditScene() const noexcept
    {
        return currentEditScene_;
    }

    bool EditorProjectSceneService::HasCurrentScene() const noexcept
    {
        return !currentScenePath_.IsEmpty() && currentSceneGuid_.IsValid();
    }

    const Path& EditorProjectSceneService::GetCurrentScenePath() const noexcept
    {
        return currentScenePath_;
    }

    const AssetGuid& EditorProjectSceneService::GetCurrentSceneGuid() const noexcept
    {
        return currentSceneGuid_;
    }

    bool EditorProjectSceneService::IsDirty() const noexcept
    {
        return dirty_;
    }

    void EditorProjectSceneService::MarkDirty() noexcept
    {
        dirty_ = true;
    }

    void EditorProjectSceneService::MarkActiveSceneEdited(bool isPlaying) noexcept
    {
        if (!isPlaying)
        {
            MarkDirty();
        }
    }

    void EditorProjectSceneService::ClearDirty() noexcept
    {
        dirty_ = false;
    }

    const EditorAuthoredAssetReference*
    EditorProjectSceneService::FindMeshRendererAssetReference(SceneObjectId gameObjectId,
                                                              SizeT componentIndex,
                                                              EditorMeshRendererAssetSlot slot) const noexcept
    {
        const auto iter = std::find_if(meshRendererAssetReferences_.begin(),
                                       meshRendererAssetReferences_.end(),
                                       [gameObjectId, componentIndex](const auto& reference)
                                       {
                                           return reference.gameObjectId == gameObjectId &&
                                                  reference.componentIndex == componentIndex;
                                       });
        if (iter == meshRendererAssetReferences_.end())
        {
            return nullptr;
        }

        const EditorAuthoredAssetReference& reference =
            slot == EditorMeshRendererAssetSlot::Mesh ? iter->mesh : iter->material;
        return reference.IsSet() ? &reference : nullptr;
    }

    void EditorProjectScriptService::Clear() noexcept
    {
        scriptContext_.reset();
        scriptHost_.reset();
    }

    ErrorCode EditorProjectScriptService::GenerateWorkspace(const Path& projectRoot,
                                                            const EditorProjectDiagnosticSink& diagnostics)
    {
        ErrorCode result = EnsureWindowsScriptWorkspace(projectRoot);
        if (result != ErrorCode::None)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "Failed to generate C# workspace: " +
                            GetWindowsScriptSolutionPath(projectRoot).GetString());
            return result;
        }

        diagnostics(EditorProjectDiagnosticSeverity::Info,
                    "Generated C# workspace: " + GetWindowsScriptSolutionPath(projectRoot).GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectScriptService::BuildScripts(const Path& projectRoot,
                                                       const EditorProjectDescriptor& descriptor,
                                                       ScriptBuildConfiguration configuration,
                                                       const EditorProjectDiagnosticSink& diagnostics)
    {
        if (!descriptor.scripting.HasWindowsScripts())
        {
            diagnostics(EditorProjectDiagnosticSeverity::Info, "Project has no Windows scripting configuration.");
            return ErrorCode::None;
        }

        const ErrorCode workspaceResult = GenerateWorkspace(projectRoot, diagnostics);
        if (workspaceResult != ErrorCode::None)
        {
            return workspaceResult;
        }

        Result<WindowsScriptBuildArtifacts> buildResult =
            BuildWindowsScriptProject(projectRoot, descriptor.scripting.windows, configuration);
        if (!buildResult)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "Script build failed: " + buildResult.GetError().GetMessage());
            return buildResult.GetError().GetCode();
        }

        diagnostics(EditorProjectDiagnosticSeverity::Info,
                    "Script build output: " + buildResult.GetValue().outputDirectory.GetString());
        return ErrorCode::None;
    }

    ErrorCode EditorProjectScriptService::PreparePlayModeScripts(const Path& projectRoot,
                                                                 const EditorProjectDescriptor& descriptor,
                                                                 ScriptBuildConfiguration configuration,
                                                                 InputSystem* inputSystem,
                                                                 Scene& playScene,
                                                                 const EditorProjectDiagnosticSink& diagnostics)
    {
        if (!descriptor.scripting.HasWindowsScripts())
        {
            return ErrorCode::None;
        }

        WindowsScriptBuildArtifacts artifacts =
            GetWindowsGeneratedScriptBuildArtifacts(projectRoot,
                                                    configuration,
                                                    descriptor.scripting.windows.assemblyName);
        if (ValidateWindowsScriptBuildArtifacts(artifacts) != ErrorCode::None)
        {
            ErrorCode buildResult = BuildScripts(projectRoot, descriptor, configuration, diagnostics);
            if (buildResult != ErrorCode::None)
            {
                return buildResult;
            }

            artifacts = GetWindowsGeneratedScriptBuildArtifacts(projectRoot,
                                                                configuration,
                                                                descriptor.scripting.windows.assemblyName);
        }

        const ErrorCode validateResult = ValidateWindowsScriptBuildArtifacts(artifacts);
        if (validateResult != ErrorCode::None)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error, "Script output is missing required runtime files.");
            return validateResult;
        }

        scriptHost_ = std::make_unique<ScriptHost>();
        Result<ScriptHostInfo> hostInfo =
            scriptHost_->Initialize(ScriptHostDesc{artifacts.projectRuntimeConfigPath,
                                                   artifacts.scriptApiAssemblyPath});
        if (!hostInfo)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "ScriptHost initialization failed: " + hostInfo.GetError().GetMessage());
            scriptHost_.reset();
            return hostInfo.GetError().GetCode();
        }

        scriptContext_ = std::make_unique<ScriptContext>(*scriptHost_);
        scriptContext_->SetRuntimeContext(inputSystem, &playScene);
        Result<ScriptOperationResult> loadResult =
            scriptContext_->LoadProjectAssembly(artifacts.projectAssemblyPath);
        if (!loadResult)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "Project script assembly load failed: " + loadResult.GetError().GetMessage());
            Clear();
            return loadResult.GetError().GetCode();
        }

        playScene.SetScriptContext(scriptContext_.get());
        diagnostics(EditorProjectDiagnosticSeverity::Info,
                    "Script context loaded: " + descriptor.scripting.windows.assemblyName);
        return ErrorCode::None;
    }

    void EditorProjectPlayModeService::Clear(EditorProjectScriptService& scriptService) noexcept
    {
        playScene_.Clear();
        playScene_.SetScriptContext(nullptr);
        scriptService.Clear();
        isPlaying_ = false;
    }

    ErrorCode EditorProjectPlayModeService::Start(const Path& projectRoot,
                                                  const EditorProjectDescriptor& descriptor,
                                                  Scene& editScene,
                                                  GameThreadSystem& gameThreadSystem,
                                                  ResourceManager& resourceManager,
                                                  EditorProjectScriptService& scriptService,
                                                  InputSystem* inputSystem,
                                                  const EditorProjectDiagnosticSink& diagnostics)
    {
        if (isPlaying_)
        {
            return ErrorCode::InvalidState;
        }

        gameThreadSystem.ClearActiveScene();

        ReflectionRegistry reflectionRegistry;
        RegisterSceneReflectionTypes(reflectionRegistry);

        ErrorCode scriptResult = scriptService.PreparePlayModeScripts(projectRoot,
                                                                      descriptor,
                                                                      ScriptBuildConfiguration::Debug,
                                                                      inputSystem,
                                                                      playScene_,
                                                                      diagnostics);
        if (scriptResult != ErrorCode::None)
        {
            scriptService.Clear();
            return scriptResult;
        }

        const std::string editSceneJson = SerializeSceneToJson(editScene, reflectionRegistry);
        ErrorCode cloneResult = DeserializeSceneFromJson(playScene_, reflectionRegistry, editSceneJson);
        if (cloneResult != ErrorCode::None)
        {
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "Failed to create play scene instance: " + std::string(ToString(cloneResult)));
            Clear(scriptService);
            return cloneResult;
        }

        playScene_.UpdateTransforms();
        isPlaying_ = true;
        const ErrorCode bindResult = gameThreadSystem.SetActiveScene(&playScene_, &resourceManager);
        if (bindResult != ErrorCode::None)
        {
            Clear(scriptService);
            diagnostics(EditorProjectDiagnosticSeverity::Error,
                        "Failed to bind play scene to the Game Thread: " + std::string(ToString(bindResult)));
            return bindResult;
        }

        diagnostics(EditorProjectDiagnosticSeverity::Info, "Play mode started.");
        return ErrorCode::None;
    }

    ErrorCode EditorProjectPlayModeService::Stop(GameThreadSystem& gameThreadSystem,
                                                 EditorProjectScriptService& scriptService,
                                                 const EditorProjectDiagnosticSink& diagnostics)
    {
        gameThreadSystem.ClearActiveScene();
        Stop(scriptService, diagnostics);
        return ErrorCode::None;
    }

    void EditorProjectPlayModeService::Stop(EditorProjectScriptService& scriptService,
                                            const EditorProjectDiagnosticSink& diagnostics)
    {
        if (!isPlaying_)
        {
            return;
        }

        Clear(scriptService);
        diagnostics(EditorProjectDiagnosticSeverity::Info, "Play mode stopped.");
    }

    Scene& EditorProjectPlayModeService::GetActiveScene(Scene& editScene) noexcept
    {
        return isPlaying_ ? playScene_ : editScene;
    }

    const Scene& EditorProjectPlayModeService::GetActiveScene(const Scene& editScene) const noexcept
    {
        return isPlaying_ ? playScene_ : editScene;
    }

    bool EditorProjectPlayModeService::IsPlaying() const noexcept
    {
        return isPlaying_;
    }

    Result<EditorProjectDescriptor> EditorProjectService::LoadProjectDescriptor(const Path& projectRoot)
    {
        if (projectRoot.IsEmpty())
        {
            return Result<EditorProjectDescriptor>::Failure(
                Error(ErrorCode::InvalidArgument, "Project root must not be empty."));
        }

        const Path descriptorPath = projectRoot / ProjectDescriptorFileName;
        if (!FileSystem::IsFile(descriptorPath))
        {
            return Result<EditorProjectDescriptor>::Failure(
                Error(ErrorCode::NotFound, "Project root does not contain .veproject: " + projectRoot.GetString()));
        }

        Result<object> jsonResult = ReadJsonObject(descriptorPath);
        if (!jsonResult)
        {
            return Result<EditorProjectDescriptor>::Failure(jsonResult.GetError());
        }

        const object& root = jsonResult.GetValue();
        if (ReadString(root, "format") != ProjectFormatName)
        {
            return Result<EditorProjectDescriptor>::Failure(
                Error(ErrorCode::InvalidArgument, "Unsupported project descriptor format."));
        }

        EditorProjectDescriptor descriptor;
        descriptor.version = ReadUInt32(root, "version", 0);
        if (descriptor.version != 1)
        {
            return Result<EditorProjectDescriptor>::Failure(
                Error(ErrorCode::Unsupported, "Unsupported project descriptor version."));
        }

        Result<AssetGuid> projectGuid = ReadGuid(root, "guid");
        if (!projectGuid || !projectGuid.GetValue().IsValid())
        {
            return Result<EditorProjectDescriptor>::Failure(
                Error(ErrorCode::InvalidArgument, "Project descriptor GUID is invalid."));
        }
        descriptor.guid = projectGuid.GetValue();

        descriptor.displayName = ReadString(root, "name");
        if (descriptor.displayName.empty())
        {
            return Result<EditorProjectDescriptor>::Failure(
                Error(ErrorCode::InvalidArgument, "Project descriptor name must not be empty."));
        }

        descriptor.engineVersion = ReadString(root, "engineVersion");
        descriptor.targetPlatforms = ReadStringArray(root, "targetPlatforms");
        if (descriptor.targetPlatforms.empty())
        {
            return Result<EditorProjectDescriptor>::Failure(
                Error(ErrorCode::InvalidArgument, "Project descriptor must list at least one target platform."));
        }

        const value* startupSceneValue = FindMember(root, "startupScene");
        if (startupSceneValue != nullptr && startupSceneValue->is_object())
        {
            const object& startupSceneJson = startupSceneValue->as_object();
            if (Result<AssetGuid> sceneGuid = ReadGuid(startupSceneJson, "guid"))
            {
                descriptor.startupScene.guid = sceneGuid.GetValue();
            }
            descriptor.startupScene.path = Path(ReadString(startupSceneJson, "path"));
        }

        if (!descriptor.startupScene.path.IsEmpty() &&
            !IsProjectRelativeAuthoredAssetPath(descriptor.startupScene.path))
        {
            return Result<EditorProjectDescriptor>::Failure(
                Error(ErrorCode::InvalidArgument, "Project startup scene must be a project-relative Assets/ path."));
        }

        Result<ScriptProjectConfig> scripting = LoadScriptProjectConfig(projectRoot);
        if (!scripting)
        {
            return Result<EditorProjectDescriptor>::Failure(scripting.GetError());
        }

        descriptor.scripting = scripting.MoveValue();

        return Result<EditorProjectDescriptor>::Success(std::move(descriptor));
    }

    ErrorCode EditorProjectService::CreateProjectSkeleton(const Path& projectRoot, std::string_view displayName)
    {
        if (projectRoot.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const Path descriptorPath = projectRoot / ProjectDescriptorFileName;
        if (FileSystem::Exists(descriptorPath))
        {
            return ErrorCode::AlreadyExists;
        }

        ErrorCode rootResult = EnsureDirectory(projectRoot);
        if (rootResult != ErrorCode::None)
        {
            return rootResult;
        }

        const Path assetsRoot = projectRoot / "Assets";
        const Path scenesRoot = projectRoot / "Assets/Scenes";
        ErrorCode assetsResult = EnsureDirectory(assetsRoot);
        if (assetsResult != ErrorCode::None)
        {
            return assetsResult;
        }

        ErrorCode scenesResult = EnsureDirectory(scenesRoot);
        if (scenesResult != ErrorCode::None)
        {
            return scenesResult;
        }

        for (const Path& generatedDirectory : GetGeneratedDirectories(projectRoot))
        {
            ErrorCode generatedResult = EnsureDirectory(generatedDirectory);
            if (generatedResult != ErrorCode::None)
            {
                return generatedResult;
            }
        }

        ErrorCode scriptWorkspaceResult = EnsureWindowsScriptWorkspace(projectRoot);
        if (scriptWorkspaceResult != ErrorCode::None)
        {
            return scriptWorkspaceResult;
        }

        const AssetGuid sceneGuid = AssetGuid::Generate();
        const Path startupScenePath("Assets/Scenes/Main.vescene");
        const Path absoluteStartupScenePath = projectRoot / startupScenePath;
        const std::string projectName =
            displayName.empty() ? MakeProjectNameFromPath(projectRoot) : std::string(displayName);

        if (FileSystem::Exists(absoluteStartupScenePath))
        {
            return ErrorCode::AlreadyExists;
        }

        ErrorCode sceneWriteResult =
            FileSystem::WriteTextFile(absoluteStartupScenePath,
                                      SerializePrettyJsonObject(WriteEmptySceneAsset(sceneGuid, "Main")));
        if (sceneWriteResult != ErrorCode::None)
        {
            return sceneWriteResult;
        }

        EditorProjectDescriptor descriptor;
        descriptor.version = 1;
        descriptor.guid = AssetGuid::Generate();
        descriptor.displayName = projectName;
        descriptor.engineVersion = GetBuildInfo().version;
        descriptor.startupScene.guid = sceneGuid;
        descriptor.startupScene.path = startupScenePath;
        descriptor.targetPlatforms = {"Windows"};

        return FileSystem::WriteTextFile(descriptorPath, boost::json::serialize(WriteProjectDescriptor(descriptor)));
    }

    ErrorCode EditorProjectService::OpenProject(const Path& projectRoot, ResourceManager& resourceManager)
    {
        ClearOpenedProject();

        if (projectRoot.IsEmpty())
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error, "Project root must not be empty.");
            return ErrorCode::InvalidArgument;
        }

        AddDiagnostic(EditorProjectDiagnosticSeverity::Info, "Opening project: " + projectRoot.GetString());

        ErrorCode directoryResult = ValidateDirectoryContract(projectRoot);
        if (directoryResult != ErrorCode::None)
        {
            return directoryResult;
        }

        ErrorCode scriptWorkspaceResult = EnsureWindowsScriptWorkspace(projectRoot);
        if (scriptWorkspaceResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Failed to create generated C# workspace: " +
                              GetWindowsScriptSolutionPath(projectRoot).GetString());
            return scriptWorkspaceResult;
        }

        Result<EditorProjectDescriptor> descriptorResult = LoadProjectDescriptor(projectRoot);
        if (!descriptorResult)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error, descriptorResult.GetError().GetMessage());
            return descriptorResult.GetError().GetCode();
        }

        ErrorCode generatedResult = EnsureGeneratedDirectories(projectRoot);
        if (generatedResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error, "Failed to create Generated/ project directories.");
            return generatedResult;
        }

        projectRoot_ = projectRoot;
        descriptor_ = descriptorResult.MoveValue();
        FileSystem::SetProjectRoot(projectRoot_);

        ErrorCode assetDatabaseResult = assetService_.Open(projectRoot_, MakeDiagnosticSink());
        if (assetDatabaseResult != ErrorCode::None)
        {
            ClearOpenedProject();
            return assetDatabaseResult;
        }

        (void)assetService_.Validate(MakeDiagnosticSink());

        ErrorCode sceneResult = OpenStartupScene(resourceManager);
        if (sceneResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          "Startup scene could not be opened. Falling back to an empty edit scene.");
            sceneService_.OpenEmptyEditScene();
        }

        hasOpenProject_ = true;
        sceneService_.ClearDirty();
        AddDiagnostic(EditorProjectDiagnosticSeverity::Info,
                      "Editor layout root: " + GetEditorLayoutPath().GetString());
        AddDiagnostic(EditorProjectDiagnosticSeverity::Info, "Project open complete: " + descriptor_.displayName);
        return ErrorCode::None;
    }

    void EditorProjectService::CloseProject(GameThreadSystem* gameThreadSystem) noexcept
    {
        if (gameThreadSystem != nullptr)
        {
            gameThreadSystem->ClearActiveScene();
        }

        ClearOpenedProject();
    }

    ErrorCode
    EditorProjectService::BindActiveScene(GameThreadSystem& gameThreadSystem, ResourceManager& resourceManager)
    {
        if (!hasOpenProject_)
        {
            return ErrorCode::InvalidState;
        }

        return gameThreadSystem.SetActiveScene(&GetActiveScene(), &resourceManager);
    }

    ErrorCode EditorProjectService::RefreshAssetDatabase()
    {
        if (!hasOpenProject_)
        {
            return ErrorCode::InvalidState;
        }

        return assetService_.Refresh(MakeDiagnosticSink());
    }

    ErrorCode EditorProjectService::GenerateScriptWorkspace()
    {
        if (!hasOpenProject_)
        {
            return ErrorCode::InvalidState;
        }

        return scriptService_.GenerateWorkspace(projectRoot_, MakeDiagnosticSink());
    }

    ErrorCode EditorProjectService::OpenScene(const Path& projectRelativeScenePath, ResourceManager& resourceManager)
    {
        if (!hasOpenProject_ || projectRelativeScenePath.IsEmpty())
        {
            return ErrorCode::InvalidState;
        }

        return sceneService_.OpenScene(projectRelativeScenePath,
                                       assetService_.GetDatabase(),
                                       resourceManager,
                                       MakeDiagnosticSink());
    }

    ErrorCode EditorProjectService::SaveCurrentScene()
    {
        if (!hasOpenProject_)
        {
            return ErrorCode::InvalidState;
        }

        return sceneService_.SaveCurrentScene(assetService_.GetDatabase(), MakeDiagnosticSink());
    }

    bool EditorProjectService::HasOpenProject() const noexcept
    {
        return hasOpenProject_;
    }

    const Path& EditorProjectService::GetProjectRoot() const noexcept
    {
        return projectRoot_;
    }

    const EditorProjectDescriptor& EditorProjectService::GetDescriptor() const noexcept
    {
        return descriptor_;
    }

    AssetDatabase& EditorProjectService::GetAssetDatabase() noexcept
    {
        return assetService_.GetDatabase();
    }

    const AssetDatabase& EditorProjectService::GetAssetDatabase() const noexcept
    {
        return assetService_.GetDatabase();
    }

    Scene& EditorProjectService::GetCurrentEditScene() noexcept
    {
        return sceneService_.GetCurrentEditScene();
    }

    const Scene& EditorProjectService::GetCurrentEditScene() const noexcept
    {
        return sceneService_.GetCurrentEditScene();
    }

    Scene& EditorProjectService::GetActiveScene() noexcept
    {
        return playModeService_.GetActiveScene(sceneService_.GetCurrentEditScene());
    }

    const Scene& EditorProjectService::GetActiveScene() const noexcept
    {
        return playModeService_.GetActiveScene(sceneService_.GetCurrentEditScene());
    }

    bool EditorProjectService::HasCurrentScene() const noexcept
    {
        return sceneService_.HasCurrentScene();
    }

    const Path& EditorProjectService::GetCurrentScenePath() const noexcept
    {
        return sceneService_.GetCurrentScenePath();
    }

    const AssetGuid& EditorProjectService::GetCurrentSceneGuid() const noexcept
    {
        return sceneService_.GetCurrentSceneGuid();
    }

    bool EditorProjectService::IsPlaying() const noexcept
    {
        return playModeService_.IsPlaying();
    }

    bool EditorProjectService::HasWindowsScripts() const noexcept
    {
        return descriptor_.scripting.HasWindowsScripts();
    }

    ErrorCode EditorProjectService::BuildScripts(ScriptBuildConfiguration configuration)
    {
        if (!hasOpenProject_)
        {
            return ErrorCode::InvalidState;
        }

        if (playModeService_.IsPlaying())
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Scripts can be rebuilt only after Play mode is stopped.");
            return ErrorCode::InvalidState;
        }

        return scriptService_.BuildScripts(projectRoot_, descriptor_, configuration, MakeDiagnosticSink());
    }

    ErrorCode
    EditorProjectService::StartPlayMode(GameThreadSystem& gameThreadSystem,
                                        ResourceManager& resourceManager,
                                        InputSystem* inputSystem)
    {
        if (!hasOpenProject_ || playModeService_.IsPlaying())
        {
            return ErrorCode::InvalidState;
        }

        return playModeService_.Start(projectRoot_,
                                      descriptor_,
                                      sceneService_.GetCurrentEditScene(),
                                      gameThreadSystem,
                                      resourceManager,
                                      scriptService_,
                                      inputSystem,
                                      MakeDiagnosticSink());
    }

    ErrorCode
    EditorProjectService::StopPlayMode(GameThreadSystem& gameThreadSystem, ResourceManager& resourceManager)
    {
        (void)resourceManager;

        const ErrorCode stopResult =
            playModeService_.Stop(gameThreadSystem, scriptService_, MakeDiagnosticSink());
        if (!hasOpenProject_)
        {
            return ErrorCode::InvalidState;
        }

        return stopResult;
    }

    void EditorProjectService::StopPlayMode()
    {
        playModeService_.Stop(scriptService_, MakeDiagnosticSink());
    }

    bool EditorProjectService::IsDirty() const noexcept
    {
        return sceneService_.IsDirty();
    }

    void EditorProjectService::MarkDirty() noexcept
    {
        sceneService_.MarkDirty();
    }

    void EditorProjectService::MarkActiveSceneEdited() noexcept
    {
        sceneService_.MarkActiveSceneEdited(playModeService_.IsPlaying());
    }

    void EditorProjectService::ClearDirty() noexcept
    {
        sceneService_.ClearDirty();
    }

    Path EditorProjectService::GetGeneratedRoot() const
    {
        return projectRoot_ / "Generated";
    }

    Path EditorProjectService::GetEditorLayoutPath() const
    {
        return projectRoot_ / "Generated/Editor/Workspace/Layout.ini";
    }

    const std::vector<EditorProjectDiagnostic>& EditorProjectService::GetDiagnostics() const noexcept
    {
        return diagnostics_;
    }

    const EditorAuthoredAssetReference*
    EditorProjectService::FindMeshRendererAssetReference(SceneObjectId gameObjectId,
                                                         SizeT componentIndex,
                                                         EditorMeshRendererAssetSlot slot) const noexcept
    {
        return sceneService_.FindMeshRendererAssetReference(gameObjectId, componentIndex, slot);
    }

    void EditorProjectService::ClearOpenedProject() noexcept
    {
        projectRoot_ = {};
        descriptor_ = {};
        assetService_.Clear();
        playModeService_.Clear(scriptService_);
        sceneService_.Clear();
        diagnostics_.clear();
        hasOpenProject_ = false;
    }

    void EditorProjectService::AddDiagnostic(EditorProjectDiagnosticSeverity severity, std::string message)
    {
        EditorProjectDiagnostic diagnostic;
        diagnostic.severity = severity;
        diagnostic.message = std::move(message);
        LogDiagnostic(diagnostic);
        diagnostics_.push_back(std::move(diagnostic));
    }

    EditorProjectDiagnosticSink EditorProjectService::MakeDiagnosticSink()
    {
        return [this](EditorProjectDiagnosticSeverity severity, std::string message)
        {
            AddDiagnostic(severity, std::move(message));
        };
    }

    ErrorCode EditorProjectService::ValidateDirectoryContract(const Path& projectRoot)
    {
        if (!FileSystem::IsDirectory(projectRoot))
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Project root is not a directory: " + projectRoot.GetString());
            return ErrorCode::NotFound;
        }

        if (!FileSystem::IsFile(projectRoot / ProjectDescriptorFileName))
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Not a VEngine project. Missing .veproject in: " + projectRoot.GetString());
            return ErrorCode::NotFound;
        }

        const Path assetsRoot = projectRoot / "Assets";
        if (!FileSystem::IsDirectory(assetsRoot))
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Project is missing the authored asset directory: Assets/");
            return ErrorCode::NotFound;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProjectService::EnsureGeneratedDirectories(const Path& projectRoot)
    {
        for (const Path& generatedDirectory : GetGeneratedDirectories(projectRoot))
        {
            ErrorCode result = EnsureDirectory(generatedDirectory);
            if (result != ErrorCode::None)
            {
                return result;
            }
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProjectService::OpenStartupScene(ResourceManager& resourceManager)
    {
        if (descriptor_.startupScene.path.IsEmpty())
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning, "Project descriptor has no startup scene.");
            return ErrorCode::NotFound;
        }

        AssetDatabase& assetDatabase = assetService_.GetDatabase();
        const Path startupScenePath = assetDatabase.ResolveProjectPath(descriptor_.startupScene.path);
        if (!FileSystem::IsFile(startupScenePath))
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          "Startup scene is missing: " + descriptor_.startupScene.path.GetString());
            return ErrorCode::NotFound;
        }

        const AssetRecord* startupRecord = assetDatabase.FindAssetByPath(descriptor_.startupScene.path);
        if (startupRecord == nullptr || startupRecord->assetType != AssetType::Scene)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          "Startup scene was not found in the AssetDatabase: " +
                              descriptor_.startupScene.path.GetString());
            return ErrorCode::NotFound;
        }

        ErrorCode sceneResult = sceneService_.OpenSceneFromRecord(*startupRecord,
                                                                  assetDatabase,
                                                                  resourceManager,
                                                                  MakeDiagnosticSink());
        if (sceneResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          std::string("Startup scene load failed: ") + ToString(sceneResult));
            return sceneResult;
        }

        return ErrorCode::None;
    }

} // namespace ve
