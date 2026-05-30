#include "Engine/Runtime/Asset/AssetDatabase.h"
#include "Engine/Runtime/Asset/NativeAssetIO.h"
#include "Engine/Runtime/Asset/SceneAssetLoader.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/RenderComponents.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Tools/AssetTool/ObjImporter.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace
{
    constexpr const char* SourceGuid = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
    constexpr const char* MaterialGuid = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb";
    constexpr const char* SceneGuid = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";

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
        if (result == ve::ErrorCode::None)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result) << '\n';
        return false;
    }

    template<typename T>
    bool ExpectOk(const ve::Result<T>& result, const char* message)
    {
        if (result)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result.GetError().GetCode());
        if (!result.GetError().GetMessage().empty())
        {
            std::cerr << ": " << result.GetError().GetMessage();
        }

        std::cerr << '\n';
        return false;
    }

    ve::Path GetTestProjectRoot()
    {
        return ve::FileSystem::GetCurrentWorkingDirectory() / "Generated/AssetPipelineTests/Project";
    }

    void RemoveTestRoot()
    {
        std::error_code error;
        std::filesystem::remove_all(std::filesystem::path("Generated") / "AssetPipelineTests", error);
    }

    void ReplaceAll(std::string& text, std::string_view token, std::string_view replacement)
    {
        size_t offset = 0;
        while ((offset = text.find(token, offset)) != std::string::npos)
        {
            text.replace(offset, token.size(), replacement);
            offset += replacement.size();
        }
    }

    bool WriteTestProject()
    {
        const ve::Path root = GetTestProjectRoot();

        const char* obj = R"(# Test cube fragment.
v -0.5 -0.5 0.0
v 0.5 -0.5 0.0
v 0.5 0.5 0.0
v -0.5 0.5 0.0
f 1 2 3 4
)";

        std::string metadata = R"json({
  "format": "VEngine.AssetMetadata",
  "version": 1,
  "guid": "$SOURCE_GUID$",
  "assetType": "SourceModel",
  "source": "Assets/Samples/Models/TestQuad.obj",
  "importer": "ObjModel",
  "importerVersion": 1,
  "settings": {
    "combineMeshes": true,
    "unitScale": 1.0,
    "generateNormals": "WhenMissing"
  },
  "artifacts": [
    {
      "type": "Mesh",
      "path": "Generated/Assets/ImportCache/$SOURCE_GUID$/TestQuad.vemesh"
    }
  ],
  "dependencies": []
})json";
        ReplaceAll(metadata, "$SOURCE_GUID$", SourceGuid);

        std::string material = R"json({
  "format": "VEngine.Material",
  "version": 1,
  "guid": "$MATERIAL_GUID$",
  "name": "TestMaterial",
  "parameters": {
    "baseColor": [0.2, 0.4, 0.8]
  },
  "textures": []
})json";
        ReplaceAll(material, "$MATERIAL_GUID$", MaterialGuid);

        std::string scene = R"json({
  "format": "VEngine.Scene",
  "version": 1,
  "guid": "$SCENE_GUID$",
  "name": "AssetPipelineTestScene",
  "scene": {
    "gameObjects": [
      {
        "id": 1,
        "name": "Camera",
        "active": true,
        "parent": 0,
        "components": [
          {
            "type": "TransformComponent",
            "properties": {
              "localPosition": [0.0, 0.0, -2.0],
              "localRotation": [0.0, 0.0, 0.0, 1.0],
              "localScale": [1.0, 1.0, 1.0]
            }
          },
          {
            "type": "CameraComponent",
            "properties": {
              "projectionMode": "Perspective",
              "fieldOfViewRadians": 1.0471976,
              "clearColor": [0.0, 0.0, 0.0, 1.0]
            }
          }
        ]
      },
      {
        "id": 2,
        "name": "ImportedQuad",
        "active": true,
        "parent": 0,
        "components": [
          {
            "type": "TransformComponent",
            "properties": {
              "localPosition": [0.0, 0.0, 0.0],
              "localRotation": [0.0, 0.0, 0.0, 1.0],
              "localScale": [1.0, 1.0, 1.0]
            }
          },
          {
            "type": "MeshRendererComponent",
            "properties": {
              "mesh": {
                "guid": "$SOURCE_GUID$",
                "path": "Assets/Samples/Models/TestQuad.obj"
              },
              "material": {
                "guid": "$MATERIAL_GUID$",
                "path": "Assets/Samples/Materials/TestMaterial.vematerial"
              },
              "visible": true
            }
          }
        ]
      }
    ]
  }
})json";
        ReplaceAll(scene, "$SCENE_GUID$", SceneGuid);
        ReplaceAll(scene, "$SOURCE_GUID$", SourceGuid);
        ReplaceAll(scene, "$MATERIAL_GUID$", MaterialGuid);

        bool passed = true;
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Assets/Samples/Models/TestQuad.obj", obj),
                           "Test OBJ should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Assets/Samples/Models/TestQuad.obj.veasset",
                                                         metadata),
                           "Test source metadata should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Assets/Samples/Materials/TestMaterial.vematerial",
                                                         material),
                           "Test material should be written");
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(root / "Assets/Samples/Scenes/TestScene.vescene", scene),
                           "Test scene should be written");
        return passed;
    }

    bool TestGuidParsing()
    {
        bool passed = true;
        ve::Result<ve::AssetGuid> guid = ve::AssetGuid::Parse(SourceGuid);
        passed &= ExpectOk(guid, "AssetGuid should parse canonical text");
        if (guid)
        {
            passed &= Expect(guid.GetValue().ToString() == SourceGuid, "AssetGuid should format canonical text");
        }

        passed &= Expect(!ve::AssetGuid::Parse("not-a-guid"), "Invalid AssetGuid text should fail");
        return passed;
    }

    bool TestSceneLoadFailsWhenMeshArtifactIsMissing()
    {
        bool passed = true;
        RemoveTestRoot();
        passed &= WriteTestProject();

        ve::AssetDatabase assetDatabase;
        passed &= ExpectOk(assetDatabase.Open(GetTestProjectRoot()), "AssetDatabase should open missing mesh project");

        ve::ResourceManager resourceManager;
        ve::ReflectionRegistry reflectionRegistry;
        ve::RegisterSceneReflectionTypes(reflectionRegistry);

        ve::Scene scene;
        const ve::ErrorCode result =
            ve::LoadSceneAsset(scene,
                               reflectionRegistry,
                               resourceManager,
                               assetDatabase,
                               assetDatabase.ResolveProjectPath(ve::Path("Assets/Samples/Scenes/TestScene.vescene")));
        passed &= Expect(result != ve::ErrorCode::None, "Scene load should fail when required .vemesh is missing");
        passed &= Expect(scene.GetGameObjectCount() == 0, "Failed scene load should not deserialize scene objects");

        RemoveTestRoot();
        return passed;
    }

    bool TestAssetPipelineRoundTrip()
    {
        bool passed = true;
        RemoveTestRoot();
        passed &= WriteTestProject();

        ve::AssetDatabase assetDatabase;
        passed &= ExpectOk(assetDatabase.Open(GetTestProjectRoot()), "AssetDatabase should open test project");
        passed &= Expect(assetDatabase.GetRecords().size() == 3, "AssetDatabase should find source, material, scene");

        ve::Result<ve::ObjImportResult> importResult =
            ve::ImportObjModel(assetDatabase, ve::Path("Assets/Samples/Models/TestQuad.obj"), true);
        passed &= ExpectOk(importResult, "OBJ import should produce a .vemesh");

        if (importResult)
        {
            passed &= Expect(importResult.GetValue().vertexCount == 6, "Quad OBJ should expand to two triangles");
            const ve::Path generatedMeshPath =
                assetDatabase.ResolveProjectPath(importResult.GetValue().meshArtifactPath);
            passed &= Expect(ve::FileSystem::IsFile(generatedMeshPath), "Imported .vemesh should exist");

            ve::Result<std::string> meshText = ve::FileSystem::ReadTextFile(generatedMeshPath);
            passed &= ExpectOk(meshText, "Imported .vemesh text should be readable");
            if (meshText)
            {
                passed &= Expect(meshText.GetValue().find("\n  \"vertexFormat\"") != std::string::npos,
                                 ".vemesh should be pretty printed with vertexFormat");
                passed &= Expect(meshText.GetValue().find("\"position\"") == std::string::npos,
                                 ".vemesh should not repeat position field names per vertex");
                passed &= Expect(meshText.GetValue().find("\"normal\"") == std::string::npos,
                                 ".vemesh should not repeat normal field names per vertex");
            }
        }

        passed &= ExpectOk(assetDatabase.Validate(), "AssetDatabase should validate after import");

        const ve::Path meshPath =
            assetDatabase.ResolveProjectPath(ve::Path("Generated/Assets/ImportCache") / SourceGuid / "TestQuad.vemesh");
        ve::Result<ve::MeshAssetData> mesh = ve::LoadMeshAsset(meshPath);
        passed &= ExpectOk(mesh, "Imported .vemesh should load");
        if (mesh)
        {
            passed &= Expect(mesh.GetValue().vertices.size() == 6, ".vemesh should contain expanded vertices");
        }

        const ve::Path legacyMeshPath = GetTestProjectRoot() / "Generated/AssetPipelineTests/Legacy.vemesh";
        const std::string legacyMesh = std::string(R"json({
  "format": "VEngine.Mesh",
  "version": 1,
  "sourceGuid": "$SOURCE_GUID$",
  "name": "Legacy",
  "vertices": [
    {
      "position": [0.0, 0.0, 0.0],
      "normal": [0.0, 1.0, 0.0],
      "color": [1.0, 1.0, 1.0]
    }
  ]
})json");
        std::string legacyMeshText = legacyMesh;
        ReplaceAll(legacyMeshText, "$SOURCE_GUID$", SourceGuid);
        passed &= ExpectOk(ve::FileSystem::WriteTextFile(legacyMeshPath, legacyMeshText),
                           "Legacy .vemesh test file should be written");
        passed &= Expect(!ve::LoadMeshAsset(legacyMeshPath),
                         "Legacy object-per-vertex .vemesh should be rejected");

        ve::ResourceManager resourceManager;
        ve::ReflectionRegistry reflectionRegistry;
        ve::RegisterSceneReflectionTypes(reflectionRegistry);

        ve::Scene scene;
        passed &= ExpectOk(ve::LoadSceneAsset(scene,
                                              reflectionRegistry,
                                              resourceManager,
                                              assetDatabase,
                                              assetDatabase.ResolveProjectPath(
                                                  ve::Path("Assets/Samples/Scenes/TestScene.vescene"))),
                           ".vescene should load and resolve asset references");

        ve::GameObject* object = scene.FindGameObject(2);
        passed &= Expect(object != nullptr, "Imported object should exist in loaded scene");
        if (object != nullptr)
        {
            ve::MeshRendererComponent* renderer = object->GetComponent<ve::MeshRendererComponent>();
            passed &= Expect(renderer != nullptr, "Imported object should have MeshRendererComponent");
            if (renderer != nullptr)
            {
                const ve::MeshResource* loadedMesh = resourceManager.FindMesh(renderer->GetMesh());
                const ve::MaterialResource* loadedMaterial = resourceManager.FindMaterial(renderer->GetMaterial());
                passed &= Expect(loadedMesh != nullptr && loadedMesh->vertices.size() == 6,
                                 "Scene loader should create mesh resource from .vemesh");
                passed &= Expect(loadedMaterial != nullptr &&
                                     loadedMaterial->baseColor.IsNearlyEqual(ve::Vector3(0.2f, 0.4f, 0.8f)),
                                 "Scene loader should create material resource from .vematerial");
            }
        }

        RemoveTestRoot();
        return passed;
    }

    bool TestImportPreservesSourceMetadataWhenOnlyImportStateChanges()
    {
        bool passed = true;
        RemoveTestRoot();
        passed &= WriteTestProject();

        ve::AssetDatabase assetDatabase;
        passed &= ExpectOk(assetDatabase.Open(GetTestProjectRoot()), "AssetDatabase should open test project");

        const ve::Path metadataPath =
            GetTestProjectRoot() / "Assets/Samples/Models/TestQuad.obj.veasset";
        ve::Result<std::string> metadataBefore = ve::FileSystem::ReadTextFile(metadataPath);
        passed &= ExpectOk(metadataBefore, "Source metadata should be readable before import");

        ve::Result<ve::ObjImportResult> importResult =
            ve::ImportObjModel(assetDatabase, ve::Path("Assets/Samples/Models/TestQuad.obj"), true);
        passed &= ExpectOk(importResult, "OBJ import should update generated import state");

        if (importResult)
        {
            const ve::Path generatedMeshPath =
                assetDatabase.ResolveProjectPath(importResult.GetValue().meshArtifactPath);
            passed &= Expect(ve::FileSystem::IsFile(generatedMeshPath),
                             "Import should produce a .vemesh");

            const ve::Path importStatePath = generatedMeshPath.GetParentPath() / "ImportState.veimportstate";
            passed &= Expect(ve::FileSystem::IsFile(importStatePath),
                             "Import should write source hash state beside generated artifacts");
        }

        ve::Result<std::string> metadataAfter = ve::FileSystem::ReadTextFile(metadataPath);
        passed &= ExpectOk(metadataAfter, "Source metadata should be readable after import");
        if (metadataBefore && metadataAfter)
        {
            passed &= Expect(metadataAfter.GetValue() == metadataBefore.GetValue(),
                             "Import should preserve source metadata text when only generated state changes");
        }

        RemoveTestRoot();
        return passed;
    }
} // namespace

int main()
{
    bool passed = true;
    passed &= TestGuidParsing();
    passed &= TestSceneLoadFailsWhenMeshArtifactIsMissing();
    passed &= TestAssetPipelineRoundTrip();
    passed &= TestImportPreservesSourceMetadataWhenOnlyImportStateChanges();

    if (!passed)
    {
        return 1;
    }

    std::cout << "VEngineAssetPipelineTests passed" << '\n';
    return 0;
}
