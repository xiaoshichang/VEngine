#include "Editor/Core/EditorProject.h"

#include "Engine/Runtime/Asset/SceneAssetLoader.h"
#include "Engine/Runtime/Core/Version.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/GameThread/GameThreadSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/Serialization/SceneSerialization.h"

#include <boost/json.hpp>
#include <boost/system/error_code.hpp>

#include <algorithm>
#include <utility>

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

        [[nodiscard]] std::vector<EditorProjectService::EditorMeshRendererAssetReferences>
        ExtractMeshRendererAssetReferences(const object& sceneAsset)
        {
            std::vector<EditorProjectService::EditorMeshRendererAssetReferences> references;
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
                    EditorProjectService::EditorMeshRendererAssetReferences entry;
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
            const std::vector<EditorProjectService::EditorMeshRendererAssetReferences>& references)
        {
            array* gameObjects = FindSceneGameObjects(sceneAsset);
            if (gameObjects == nullptr)
            {
                return;
            }

            for (const EditorProjectService::EditorMeshRendererAssetReferences& reference : references)
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
                                      boost::json::serialize(WriteEmptySceneAsset(sceneGuid, "Main")));
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

        ErrorCode assetDatabaseResult = assetDatabase_.Open(projectRoot_);
        if (assetDatabaseResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          std::string("AssetDatabase open failed: ") + ToString(assetDatabaseResult));
            ClearOpenedProject();
            return assetDatabaseResult;
        }

        AddDiagnostic(EditorProjectDiagnosticSeverity::Info,
                      "AssetDatabase refreshed " + std::to_string(assetDatabase_.GetRecords().size()) + " asset(s).");

        ErrorCode validationResult = assetDatabase_.Validate();
        if (validationResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          "AssetDatabase validation reported missing generated artifacts or source files.");
        }

        ErrorCode sceneResult = OpenStartupScene(resourceManager);
        if (sceneResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          "Startup scene could not be opened. Falling back to an empty edit scene.");
            OpenEmptyEditScene();
        }

        hasOpenProject_ = true;
        dirty_ = false;
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

        return gameThreadSystem.SetActiveScene(&currentEditScene_, &resourceManager);
    }

    ErrorCode EditorProjectService::RefreshAssetDatabase()
    {
        if (!hasOpenProject_)
        {
            return ErrorCode::InvalidState;
        }

        const ErrorCode refreshResult = assetDatabase_.Refresh();
        if (refreshResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          std::string("AssetDatabase refresh failed: ") + ToString(refreshResult));
            return refreshResult;
        }

        AddDiagnostic(EditorProjectDiagnosticSeverity::Info,
                      "AssetDatabase refreshed " + std::to_string(assetDatabase_.GetRecords().size()) + " asset(s).");
        return ErrorCode::None;
    }

    ErrorCode EditorProjectService::OpenScene(const Path& projectRelativeScenePath, ResourceManager& resourceManager)
    {
        if (!hasOpenProject_ || projectRelativeScenePath.IsEmpty())
        {
            return ErrorCode::InvalidState;
        }

        const AssetRecord* record = assetDatabase_.FindAssetByPath(projectRelativeScenePath);
        if (record == nullptr)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Scene asset was not found: " + projectRelativeScenePath.GetString());
            return ErrorCode::NotFound;
        }

        if (record->assetType != AssetType::Scene)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Asset is not a scene: " + projectRelativeScenePath.GetString());
            return ErrorCode::InvalidArgument;
        }

        return LoadSceneFromRecord(*record, resourceManager);
    }

    ErrorCode EditorProjectService::SaveCurrentScene()
    {
        if (!hasOpenProject_)
        {
            return ErrorCode::InvalidState;
        }

        if (currentScenePath_.IsEmpty() || !currentSceneGuid_.IsValid())
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Current edit scene has no authored .vescene asset to save.");
            return ErrorCode::InvalidState;
        }

        ReflectionRegistry reflectionRegistry;
        RegisterSceneReflectionTypes(reflectionRegistry);

        Result<object> sceneAsset =
            MakeSceneAssetJson(currentEditScene_, reflectionRegistry, currentSceneGuid_, currentScenePath_);
        if (!sceneAsset)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error, sceneAsset.GetError().GetMessage());
            return sceneAsset.GetError().GetCode();
        }

        const Path scenePath = assetDatabase_.ResolveProjectPath(currentScenePath_);
        if (Result<object> previousSceneAsset = ReadJsonObject(scenePath))
        {
            PreserveSceneAssetReferences(sceneAsset.GetValue(), previousSceneAsset.GetValue());
        }

        ApplyMeshRendererAssetReferences(sceneAsset.GetValue(), meshRendererAssetReferences_);

        const ErrorCode writeResult =
            FileSystem::WriteTextFile(scenePath, boost::json::serialize(sceneAsset.GetValue()));
        if (writeResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Failed to save scene: " + currentScenePath_.GetString());
            return writeResult;
        }

        meshRendererAssetReferences_ = ExtractMeshRendererAssetReferences(sceneAsset.GetValue());
        dirty_ = false;
        const ErrorCode refreshResult = assetDatabase_.Refresh();
        if (refreshResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          "Scene saved, but AssetDatabase refresh failed: " + std::string(ToString(refreshResult)));
            return refreshResult;
        }

        AddDiagnostic(EditorProjectDiagnosticSeverity::Info, "Saved scene: " + currentScenePath_.GetString());
        return ErrorCode::None;
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
        return assetDatabase_;
    }

    const AssetDatabase& EditorProjectService::GetAssetDatabase() const noexcept
    {
        return assetDatabase_;
    }

    Scene& EditorProjectService::GetCurrentEditScene() noexcept
    {
        return currentEditScene_;
    }

    const Scene& EditorProjectService::GetCurrentEditScene() const noexcept
    {
        return currentEditScene_;
    }

    Scene& EditorProjectService::GetActiveScene() noexcept
    {
        return isPlaying_ ? playScene_ : currentEditScene_;
    }

    const Scene& EditorProjectService::GetActiveScene() const noexcept
    {
        return isPlaying_ ? playScene_ : currentEditScene_;
    }

    bool EditorProjectService::HasCurrentScene() const noexcept
    {
        return !currentScenePath_.IsEmpty() && currentSceneGuid_.IsValid();
    }

    const Path& EditorProjectService::GetCurrentScenePath() const noexcept
    {
        return currentScenePath_;
    }

    const AssetGuid& EditorProjectService::GetCurrentSceneGuid() const noexcept
    {
        return currentSceneGuid_;
    }

    bool EditorProjectService::IsPlaying() const noexcept
    {
        return isPlaying_;
    }

    ErrorCode EditorProjectService::StartPlayMode(ResourceManager& resourceManager)
    {
        (void)resourceManager;

        if (!hasOpenProject_ || isPlaying_)
        {
            return ErrorCode::InvalidState;
        }

        ReflectionRegistry reflectionRegistry;
        RegisterSceneReflectionTypes(reflectionRegistry);

        const std::string editSceneJson = SerializeSceneToJson(currentEditScene_, reflectionRegistry);
        ErrorCode cloneResult = DeserializeSceneFromJson(playScene_, reflectionRegistry, editSceneJson);
        if (cloneResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Failed to create play scene instance: " + std::string(ToString(cloneResult)));
            playScene_.Clear();
            return cloneResult;
        }

        playScene_.UpdateTransforms();
        isPlaying_ = true;
        AddDiagnostic(EditorProjectDiagnosticSeverity::Info, "Play mode started.");
        return ErrorCode::None;
    }

    void EditorProjectService::StopPlayMode()
    {
        if (!isPlaying_)
        {
            return;
        }

        playScene_.Clear();
        isPlaying_ = false;
        AddDiagnostic(EditorProjectDiagnosticSeverity::Info, "Play mode stopped.");
    }

    void EditorProjectService::TickPlayMode()
    {
        if (!isPlaying_)
        {
            return;
        }

        playScene_.Update();
        playScene_.LateUpdate();
        playScene_.UpdateTransforms();
    }

    bool EditorProjectService::IsDirty() const noexcept
    {
        return dirty_;
    }

    void EditorProjectService::MarkDirty() noexcept
    {
        dirty_ = true;
    }

    void EditorProjectService::MarkActiveSceneEdited() noexcept
    {
        if (!isPlaying_)
        {
            MarkDirty();
        }
    }

    void EditorProjectService::ClearDirty() noexcept
    {
        dirty_ = false;
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

    void EditorProjectService::ClearOpenedProject() noexcept
    {
        projectRoot_ = {};
        descriptor_ = {};
        assetDatabase_ = {};
        playScene_.Clear();
        currentEditScene_.Clear();
        currentScenePath_ = {};
        currentSceneGuid_ = {};
        meshRendererAssetReferences_.clear();
        diagnostics_.clear();
        hasOpenProject_ = false;
        isPlaying_ = false;
        dirty_ = false;
    }

    void EditorProjectService::AddDiagnostic(EditorProjectDiagnosticSeverity severity, std::string message)
    {
        EditorProjectDiagnostic diagnostic;
        diagnostic.severity = severity;
        diagnostic.message = std::move(message);
        LogDiagnostic(diagnostic);
        diagnostics_.push_back(std::move(diagnostic));
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

        const Path startupScenePath = assetDatabase_.ResolveProjectPath(descriptor_.startupScene.path);
        if (!FileSystem::IsFile(startupScenePath))
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          "Startup scene is missing: " + descriptor_.startupScene.path.GetString());
            return ErrorCode::NotFound;
        }

        const AssetRecord* startupRecord = assetDatabase_.FindAssetByPath(descriptor_.startupScene.path);
        if (startupRecord == nullptr || startupRecord->assetType != AssetType::Scene)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          "Startup scene was not found in the AssetDatabase: " +
                              descriptor_.startupScene.path.GetString());
            return ErrorCode::NotFound;
        }

        ErrorCode sceneResult = LoadSceneFromRecord(*startupRecord, resourceManager);
        if (sceneResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Warning,
                          std::string("Startup scene load failed: ") + ToString(sceneResult));
            return sceneResult;
        }

        return ErrorCode::None;
    }

    ErrorCode EditorProjectService::LoadSceneFromRecord(const AssetRecord& record, ResourceManager& resourceManager)
    {
        if (record.assetType != AssetType::Scene || !record.guid.IsValid() || record.path.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const Path scenePath = assetDatabase_.ResolveProjectPath(record.path);
        if (!FileSystem::IsFile(scenePath))
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error, "Scene file is missing: " + record.path.GetString());
            return ErrorCode::NotFound;
        }

        ReflectionRegistry reflectionRegistry;
        RegisterSceneReflectionTypes(reflectionRegistry);

        Result<object> authoredSceneAsset = ReadJsonObject(scenePath);
        if (!authoredSceneAsset)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Scene parse failed: " + record.path.GetString());
            return authoredSceneAsset.GetError().GetCode();
        }

        const ErrorCode sceneResult =
            LoadSceneAsset(currentEditScene_, reflectionRegistry, resourceManager, assetDatabase_, scenePath);
        if (sceneResult != ErrorCode::None)
        {
            AddDiagnostic(EditorProjectDiagnosticSeverity::Error,
                          "Scene load failed: " + record.path.GetString() + " (" + ToString(sceneResult) + ")");
            return sceneResult;
        }

        currentScenePath_ = record.path;
        currentSceneGuid_ = record.guid;
        meshRendererAssetReferences_ = ExtractMeshRendererAssetReferences(authoredSceneAsset.GetValue());
        dirty_ = false;
        AddDiagnostic(EditorProjectDiagnosticSeverity::Info, "Opened scene: " + currentScenePath_.GetString());
        return ErrorCode::None;
    }

    void EditorProjectService::OpenEmptyEditScene()
    {
        currentEditScene_.Clear();
        currentEditScene_.UpdateTransforms();
        currentScenePath_ = {};
        currentSceneGuid_ = {};
        meshRendererAssetReferences_.clear();
    }
} // namespace ve
