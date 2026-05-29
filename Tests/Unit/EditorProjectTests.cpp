#include "Editor/Core/EditorProject.h"
#include "Editor/Core/EditorReflection.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Physics/ColliderComponent.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Resource/BuiltInResources.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool ExpectOk(ve::ErrorCode result, const char* message)
    {
        if (result != ve::ErrorCode::None)
        {
            std::cerr << "FAILED: " << message << " (" << ve::ToString(result) << ")" << '\n';
            return false;
        }

        return true;
    }

    template<typename T>
    bool ExpectOk(const ve::Result<T>& result, const char* message)
    {
        if (!result)
        {
            std::cerr << "FAILED: " << message << " (" << result.GetError().GetMessage() << ")" << '\n';
            return false;
        }

        return true;
    }

    ve::Path GetTestRoot()
    {
        return ve::FileSystem::GetCurrentWorkingDirectory() / "Generated/EditorProjectTests";
    }

    void CleanTestRoot()
    {
        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path("Generated") / "EditorProjectTests", error);
    }

    void ReplaceAll(std::string& text, std::string_view from, std::string_view to)
    {
        size_t offset = 0;
        while ((offset = text.find(from, offset)) != std::string::npos)
        {
            text.replace(offset, from.size(), to);
            offset += to.size();
        }
    }

    bool TestCreateAndOpenProject()
    {
        bool passed = true;

        CleanTestRoot();

        const ve::Path projectRoot = GetTestRoot() / "Project";
        passed &= ExpectOk(ve::EditorProjectService::CreateProjectSkeleton(projectRoot, "EditorProjectTest"),
                           "CreateProjectSkeleton should create a project");
        passed &= Expect(ve::FileSystem::IsFile(projectRoot / ".veproject"), "Project descriptor should be created");
        passed &= Expect(ve::FileSystem::IsDirectory(projectRoot / "Assets"), "Assets directory should be created");
        passed &= Expect(ve::FileSystem::IsDirectory(projectRoot / "Generated/Build"),
                         "Generated/Build should be created");
        passed &= Expect(ve::FileSystem::IsDirectory(projectRoot / "Generated/Editor/Workspace"),
                         "Generated/Editor/Workspace should be created");
        passed &= Expect(ve::FileSystem::IsDirectory(ve::GetWindowsScriptSourceDirectory(projectRoot)),
                         "Fixed VE.Scripting source directory should be created");
        passed &= Expect(ve::FileSystem::IsFile(ve::GetWindowsScriptProjectPath(projectRoot)),
                         "Generated VE.Scripting project should be created");
        passed &= Expect(ve::FileSystem::IsFile(ve::GetWindowsScriptSolutionPath(projectRoot)),
                         "Generated VE.Scripting solution should be created");

        ve::Result<ve::EditorProjectDescriptor> descriptor =
            ve::EditorProjectService::LoadProjectDescriptor(projectRoot);
        passed &= ExpectOk(descriptor, "Project descriptor should parse");
        if (descriptor)
        {
            passed &= Expect(descriptor.GetValue().displayName == "EditorProjectTest",
                             "Project descriptor should preserve display name");
            passed &= Expect(descriptor.GetValue().guid.IsValid(), "Project descriptor should have project GUID");
            passed &= Expect(!descriptor.GetValue().engineVersion.empty(),
                             "Project descriptor should have engine version");
            passed &= Expect(!descriptor.GetValue().targetPlatforms.empty(),
                             "Project descriptor should have target platforms");
            passed &= Expect(descriptor.GetValue().startupScene.path == ve::Path("Assets/Scenes/Main.vescene"),
                             "Project descriptor should point at startup scene");
            passed &= Expect(descriptor.GetValue().scripting.HasWindowsScripts(),
                             "Fixed VE.Scripting source folder should enable Windows scripts");
            passed &= Expect(descriptor.GetValue().scripting.windows.projectPath ==
                                 ve::GetWindowsScriptProjectRelativePath(),
                             "Descriptor should use the generated Windows script project path");
            passed &= Expect(descriptor.GetValue().scripting.windows.assemblyName ==
                                 std::string(ve::GetWindowsScriptAssemblyName()),
                             "Descriptor should use the fixed Windows script assembly name");
        }

        ve::ResourceManager resourceManager;
        ve::EditorProjectService projectService;
        std::error_code scriptProjectRemoveError;
        std::filesystem::remove(std::filesystem::path(ve::GetWindowsScriptProjectPath(projectRoot).GetString()),
                                scriptProjectRemoveError);
        std::error_code scriptSolutionRemoveError;
        std::filesystem::remove(std::filesystem::path(ve::GetWindowsScriptSolutionPath(projectRoot).GetString()),
                                scriptSolutionRemoveError);
        passed &= Expect(!ve::FileSystem::IsFile(ve::GetWindowsScriptProjectPath(projectRoot)),
                         "Test setup should remove the generated script project");
        passed &= Expect(!ve::FileSystem::IsFile(ve::GetWindowsScriptSolutionPath(projectRoot)),
                         "Test setup should remove the generated script solution");
        passed &= ExpectOk(projectService.OpenProject(projectRoot, resourceManager),
                           "EditorProjectService should open created project");
        passed &= Expect(projectService.HasOpenProject(), "Project service should report an open project");
        passed &= Expect(projectService.GetProjectRoot() == projectRoot, "Project root should be stored");
        passed &= Expect(ve::FileSystem::IsFile(ve::GetWindowsScriptProjectPath(projectRoot)),
                         "OpenProject should restore the generated VE.Scripting project");
        passed &= Expect(ve::FileSystem::IsFile(ve::GetWindowsScriptSolutionPath(projectRoot)),
                         "OpenProject should restore the generated VE.Scripting solution");
        passed &= Expect(projectService.HasWindowsScripts(), "Opened project should report fixed Windows scripts");
        passed &= Expect(projectService.GetAssetDatabase().GetRecords().size() == 1,
                         "AssetDatabase should discover the startup scene");
        passed &= Expect(projectService.HasCurrentScene(), "Startup scene should be recorded as the current scene");
        passed &= Expect(projectService.GetCurrentScenePath() == ve::Path("Assets/Scenes/Main.vescene"),
                         "Current scene path should point at the startup scene");
        passed &= Expect(projectService.GetCurrentSceneGuid() == descriptor.GetValue().startupScene.guid,
                         "Current scene GUID should match the startup scene GUID");
        passed &= Expect(projectService.GetCurrentEditScene().GetGameObjectCount() == 0,
                         "Empty startup scene should load as an empty edit scene");
        passed &= Expect(projectService.GetEditorLayoutPath() ==
                             (projectRoot / "Generated/Editor/Workspace/Layout.ini"),
                         "Editor layout path should live under Generated/Editor");
        passed &= Expect(!projectService.IsDirty(), "Opened project should not be dirty");

        projectService.MarkDirty();
        passed &= Expect(projectService.IsDirty(), "MarkDirty should set dirty state");
        projectService.ClearDirty();
        passed &= Expect(!projectService.IsDirty(), "ClearDirty should reset dirty state");

        ve::GameObject& gameObject = projectService.GetCurrentEditScene().CreateGameObject("SavedObject");
        ve::TransformComponent& transform = gameObject.AddComponent<ve::TransformComponent>();
        transform.SetLocalPosition(ve::Vector3(2.1f, 0.425f, 3.15f));
        transform.SetLocalScale(ve::Vector3(0.85f, 0.85f, 0.85f));
        projectService.GetCurrentEditScene().UpdateTransforms();
        projectService.MarkDirty();
        passed &= ExpectOk(projectService.SaveCurrentScene(), "SaveCurrentScene should write the current scene");
        passed &= Expect(!projectService.IsDirty(), "SaveCurrentScene should clear dirty state");

        ve::Result<std::string> savedScene = ve::FileSystem::ReadTextFile(projectRoot / "Assets/Scenes/Main.vescene");
        passed &= ExpectOk(savedScene, "Saved scene should be readable");
        if (savedScene)
        {
            passed &= Expect(savedScene.GetValue().find("SavedObject") != std::string::npos,
                             "Saved scene should contain edited GameObject name");
            passed &= Expect(savedScene.GetValue().ends_with('\n'), "Saved scene should end with a newline");
            passed &= Expect(savedScene.GetValue().find("\n  \"format\": \"VEngine.Scene\"") != std::string::npos,
                             "Saved scene should keep top-level indentation");
            passed &= Expect(savedScene.GetValue().find("\n    \"gameObjects\": [") != std::string::npos,
                             "Saved scene object list should be indented");
            passed &= Expect(savedScene.GetValue().find("\"localPosition\": [2.1, 0.425, 3.15]") !=
                                 std::string::npos,
                             "Saved scene should use concise decimal transform values");
            passed &= Expect(savedScene.GetValue().find("\"localScale\": [0.85, 0.85, 0.85]") != std::string::npos,
                             "Saved scene should trim trailing decimal zeroes");
            passed &= Expect(savedScene.GetValue().find("2.0999999046325684E0") == std::string::npos,
                             "Saved scene should not use verbose Float32 round-trip text");
        }

        passed &= ExpectOk(projectService.OpenScene(ve::Path("Assets/Scenes/Main.vescene"), resourceManager),
                           "OpenScene should load a saved .vescene asset");
        passed &= Expect(projectService.GetCurrentEditScene().GetGameObjectCount() == 1,
                         "Saved scene should reopen with the edited GameObject");

        CleanTestRoot();
        return passed;
    }

    bool TestMeshRendererAuthoredReferences()
    {
        bool passed = true;

        CleanTestRoot();

        const ve::Path projectRoot = GetTestRoot() / "MeshRendererReferences";
        passed &= ExpectOk(ve::EditorProjectService::CreateProjectSkeleton(projectRoot, "MeshRendererReferences"),
                           "CreateProjectSkeleton should create a reference test project");

        ve::Result<ve::EditorProjectDescriptor> descriptor =
            ve::EditorProjectService::LoadProjectDescriptor(projectRoot);
        passed &= ExpectOk(descriptor, "Reference test project descriptor should parse");
        if (!descriptor)
        {
            CleanTestRoot();
            return false;
        }

        std::string scene = R"json({
  "format": "VEngine.Scene",
  "version": 1,
  "guid": "$SCENE_GUID$",
  "name": "Main",
  "scene": {
    "gameObjects": [
      {
        "id": 1,
        "name": "AuthoredMeshRenderer",
        "active": true,
        "parent": 0,
        "components": [
          {
            "type": "MeshRendererComponent",
            "properties": {
              "mesh": {
                "path": "builtin:mesh/cube"
              },
              "material": {
                "path": "builtin:material/default"
              },
              "visible": true
            }
          }
        ]
      }
    ]
  }
})json";
        ReplaceAll(scene, "$SCENE_GUID$", descriptor.GetValue().startupScene.guid.ToString());
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(projectRoot / "Assets/Scenes/Main.vescene", scene),
                           "Reference test scene should be written");

        ve::ResourceManager resourceManager;
        ve::EditorProjectService projectService;
        passed &= ExpectOk(projectService.OpenProject(projectRoot, resourceManager),
                           "EditorProjectService should open authored reference scene");

        const ve::EditorAuthoredAssetReference* meshReference =
            projectService.FindMeshRendererAssetReference(1, 0, ve::EditorMeshRendererAssetSlot::Mesh);
        passed &= Expect(meshReference != nullptr, "MeshRenderer mesh authored reference should be indexed");
        if (meshReference != nullptr)
        {
            passed &= Expect(meshReference->path.GetString() == ve::BuiltInResources::FallbackCubeMeshUri,
                             "Mesh authored reference should preserve built-in mesh URI");
        }

        const ve::EditorAuthoredAssetReference* materialReference =
            projectService.FindMeshRendererAssetReference(1, 0, ve::EditorMeshRendererAssetSlot::Material);
        passed &= Expect(materialReference != nullptr, "MeshRenderer material authored reference should be indexed");
        if (materialReference != nullptr)
        {
            passed &= Expect(materialReference->path.GetString() == ve::BuiltInResources::DefaultMaterialUri,
                             "Material authored reference should preserve built-in material URI");
        }

        passed &= ExpectOk(projectService.SaveCurrentScene(),
                           "SaveCurrentScene should write authored references instead of runtime resource IDs");
        ve::Result<std::string> savedScene =
            ve::FileSystem::ReadTextFile(projectRoot / "Assets/Scenes/Main.vescene");
        passed &= ExpectOk(savedScene, "Saved authored reference scene should be readable");
        if (savedScene)
        {
            passed &= Expect(savedScene.GetValue().find("builtin:mesh/cube") != std::string::npos,
                             "Saved scene should keep authored mesh reference");
            passed &= Expect(savedScene.GetValue().find("builtin:material/default") != std::string::npos,
                             "Saved scene should keep authored material reference");
            passed &= Expect(savedScene.GetValue().find("\"mesh\":1") == std::string::npos,
                             "Saved scene should not write mesh runtime ID");
            passed &= Expect(savedScene.GetValue().find("\"material\":2") == std::string::npos,
                             "Saved scene should not write material runtime ID");
        }

        CleanTestRoot();
        return passed;
    }

    bool TestProjectDescriptorRejectsScriptingSection()
    {
        bool passed = true;

        CleanTestRoot();

        const ve::Path projectRoot = GetTestRoot() / "ScriptingDescriptor";
        passed &= ExpectOk(ve::EditorProjectService::CreateProjectSkeleton(projectRoot, "ScriptingDescriptor"),
                           "CreateProjectSkeleton should create a scripting descriptor test project");

        ve::Result<std::string> projectText = ve::FileSystem::ReadTextFile(projectRoot / ".veproject");
        passed &= ExpectOk(projectText, "Project descriptor should be readable");
        if (!projectText)
        {
            CleanTestRoot();
            return false;
        }

        std::string descriptor = projectText.GetValue();
        ReplaceAll(descriptor,
                   "\"targetPlatforms\"",
                   "\"scripting\":{\"windows\":{\"project\":\"Generated/Editor/Workspace/VE.Scripting.csproj\","
                   "\"assemblyName\":\"VE.Scripting\"}},\"targetPlatforms\"");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(projectRoot / ".veproject", descriptor),
                           "Project descriptor with legacy scripting config should be written");

        ve::Result<ve::EditorProjectDescriptor> rejected =
            ve::EditorProjectService::LoadProjectDescriptor(projectRoot);
        passed &= Expect(!rejected, "Project descriptor scripting sections should be rejected");

        CleanTestRoot();
        return passed;
    }

    bool TestPlayModeUsesDiscardableSceneInstance()
    {
        bool passed = true;

        CleanTestRoot();

        const ve::Path projectRoot = GetTestRoot() / "PlayModeSceneInstance";
        passed &= ExpectOk(ve::EditorProjectService::CreateProjectSkeleton(projectRoot, "PlayModeSceneInstance"),
                           "CreateProjectSkeleton should create a play mode test project");

        ve::ResourceManager resourceManager;
        ve::EditorProjectService projectService;
        passed &= ExpectOk(projectService.OpenProject(projectRoot, resourceManager),
                           "EditorProjectService should open play mode test project");

        ve::GameObject& editObject = projectService.GetCurrentEditScene().CreateGameObject("EditObject");
        editObject.AddComponent<ve::TransformComponent>();
        projectService.GetCurrentEditScene().UpdateTransforms();
        projectService.ClearDirty();

        passed &= ExpectOk(projectService.StartPlayMode(resourceManager), "StartPlayMode should clone the edit scene");
        passed &= Expect(projectService.IsPlaying(), "Project service should report play mode");
        passed &= Expect(projectService.GetActiveScene().GetGameObjectCount() == 1,
                         "Play scene should start as a clone of the edit scene");
        passed &= Expect(projectService.BuildScripts(ve::ScriptBuildConfiguration::Debug) ==
                             ve::ErrorCode::InvalidState,
                         "BuildScripts should be blocked while Play mode is running");

        ve::GameObject& playObject = projectService.GetActiveScene().CreateGameObject("PlayOnlyObject");
        playObject.AddComponent<ve::TransformComponent>();
        const ve::SceneObjectId playObjectId = playObject.GetId();
        projectService.MarkActiveSceneEdited();
        passed &= Expect(!projectService.IsDirty(), "Play scene edits should not dirty the authored edit scene");
        passed &= Expect(projectService.GetActiveScene().GetGameObjectCount() == 2,
                         "Play scene should accept runtime edits");
        passed &= Expect(projectService.GetCurrentEditScene().GetGameObjectCount() == 1,
                         "Edit scene should not receive play mode objects");

        projectService.StopPlayMode();
        passed &= Expect(!projectService.IsPlaying(), "StopPlayMode should leave play mode");
        passed &= Expect(projectService.GetActiveScene().GetGameObjectCount() == 1,
                         "Stopping play mode should restore the edit scene as active");
        passed &= Expect(projectService.GetCurrentEditScene().FindGameObject(playObjectId) == nullptr,
                         "Stopping play mode should discard play scene objects");

        CleanTestRoot();
        return passed;
    }

    bool TestEditorReflectionSupportsColliderAuthoring()
    {
        ve::ReflectionRegistry reflectionRegistry;
        ve::RegisterSceneReflectionTypes(reflectionRegistry);

        const ve::ReflectedTypeInfo* colliderType = reflectionRegistry.FindType("ColliderComponent");

        bool passed = true;
        passed &= Expect(colliderType != nullptr, "ColliderComponent should be available to editor reflection");
        passed &= Expect(colliderType != nullptr && colliderType->componentFactory != nullptr,
                         "ColliderComponent should be addable from reflected component factory");

        bool sawLayer = false;
        bool sawCollidesWith = false;
        if (colliderType != nullptr)
        {
            for (const ve::ReflectedPropertyInfo& property : colliderType->properties)
            {
                if (property.name == "layer")
                {
                    sawLayer = true;
                    passed &= Expect(property.type == ve::ReflectedPropertyType::UInt64,
                                     "Collider layer should be a UInt64 reflected property");
                    passed &= Expect(ve::IsEditorEditableReflectedPropertyType(property.type),
                                     "Editor should edit Collider layer");
                }
                else if (property.name == "collidesWith")
                {
                    sawCollidesWith = true;
                    passed &= Expect(property.type == ve::ReflectedPropertyType::UInt64,
                                     "Collider collision mask should be a UInt64 reflected property");
                    passed &= Expect(ve::IsEditorEditableReflectedPropertyType(property.type),
                                     "Editor should edit Collider collision mask");
                }
            }
        }

        passed &= Expect(sawLayer, "Collider layer property should be reflected");
        passed &= Expect(sawCollidesWith, "Collider collision mask property should be reflected");

        ve::Scene scene;
        ve::GameObject& object = scene.CreateGameObject("ColliderAuthoring");
        object.AddComponent<ve::TransformComponent>();
        passed &= Expect(colliderType != nullptr &&
                             ve::CanAddReflectedComponentToGameObject(reflectionRegistry, object, *colliderType),
                         "Editor should allow adding the first ColliderComponent");
        object.AddComponent<ve::ColliderComponent>();
        passed &= Expect(colliderType != nullptr &&
                             !ve::CanAddReflectedComponentToGameObject(reflectionRegistry, object, *colliderType),
                         "Editor should disable adding a duplicate ColliderComponent");

        const ve::ReflectedTypeInfo* transformType = reflectionRegistry.FindType("TransformComponent");
        passed &= Expect(transformType != nullptr &&
                             !ve::CanAddReflectedComponentToGameObject(reflectionRegistry, object, *transformType),
                         "Editor should still disable adding a duplicate TransformComponent");
        return passed;
    }

    bool TestRejectsMissingDescriptor()
    {
        bool passed = true;

        CleanTestRoot();
        const ve::Path projectRoot = GetTestRoot() / "MissingDescriptor";
        passed &= ExpectOk(ve::FileSystem::CreateDirectories(projectRoot / "Assets"),
                           "Test project Assets directory should be created");

        ve::ResourceManager resourceManager;
        ve::EditorProjectService projectService;
        passed &= Expect(projectService.OpenProject(projectRoot, resourceManager) == ve::ErrorCode::NotFound,
                         "OpenProject should reject roots without .veproject");
        passed &= Expect(!projectService.HasOpenProject(), "Rejected project should not stay open");

        CleanTestRoot();
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestCreateAndOpenProject();
    passed &= TestMeshRendererAuthoredReferences();
    passed &= TestProjectDescriptorRejectsScriptingSection();
    passed &= TestPlayModeUsesDiscardableSceneInstance();
    passed &= TestEditorReflectionSupportsColliderAuthoring();
    passed &= TestRejectsMissingDescriptor();

    if (passed)
    {
        std::cout << "VEngineEditorProjectTests passed" << '\n';
        return 0;
    }

    return 1;
}
