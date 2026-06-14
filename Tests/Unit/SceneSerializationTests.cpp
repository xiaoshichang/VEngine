#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Quaternion.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Scene/CameraComponent.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/LightComponent.h"
#include "Engine/Runtime/Scene/MeshRenderComponent.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneSerialization.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <iostream>
#include <string>

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
        if (result == ve::ErrorCode::None)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result) << '\n';
        return false;
    }

    bool TestSceneRoundTrip()
    {
        bool passed = true;

        ve::Scene sourceScene("Serialization Smoke");
        ve::Result<ve::GameObject*> rootResult = sourceScene.CreateRootGameObject("Root");
        passed &= Expect(rootResult.IsOk(), "Root GameObject should be created");
        ve::GameObject* root = rootResult.GetValue();

        ve::TransformComponent* rootTransform = root->GetComponent<ve::TransformComponent>();
        passed &= Expect(rootTransform != nullptr, "Root should have a TransformComponent");
        rootTransform->SetLocalPosition(ve::Vector3(1.0f, 2.0f, 3.0f));
        rootTransform->SetLocalScale(ve::Vector3(2.0f, 2.0f, 2.0f));

        ve::Result<ve::MeshRenderComponent*> meshResult = root->AddComponentWithoutRenderRegistration<ve::MeshRenderComponent>();
        passed &= Expect(meshResult.IsOk(), "MeshRenderComponent should be added");
        ve::MeshRenderComponent* mesh = meshResult.GetValue();
        mesh->SetMeshAssetGuid("11111111-1111-1111-1111-111111111111");
        mesh->SetMaterialAssetGuid("22222222-2222-2222-2222-222222222222");
        mesh->SetBoundsCenter(ve::Vector3(0.0f, 0.5f, 0.0f));
        mesh->SetBoundsExtents(ve::Vector3(1.0f, 2.0f, 3.0f));

        ve::Result<ve::GameObject*> cameraObjectResult = rootTransform->CreateChild("Camera");
        passed &= Expect(cameraObjectResult.IsOk(), "Camera GameObject should be created");
        ve::GameObject* cameraObject = cameraObjectResult.GetValue();
        ve::TransformComponent* cameraTransform = cameraObject->GetComponent<ve::TransformComponent>();
        cameraTransform->SetLocalPosition(ve::Vector3(0.0f, 1.0f, -5.0f));
        ve::Result<ve::CameraComponent*> cameraResult =
            cameraObject->AddComponentWithoutRenderRegistration<ve::CameraComponent>();
        passed &= Expect(cameraResult.IsOk(), "CameraComponent should be added");
        ve::CameraComponent* camera = cameraResult.GetValue();
        camera->SetPrimary(true);
        camera->SetProjectionMode(ve::CameraComponent::ProjectionMode::Orthographic);
        camera->SetOrthographicSize(8.0f);

        ve::Result<ve::GameObject*> lightObjectResult = sourceScene.CreateRootGameObject("Key Light");
        passed &= Expect(lightObjectResult.IsOk(), "Light GameObject should be created");
        ve::GameObject* lightObject = lightObjectResult.GetValue();
        ve::Result<ve::LightComponent*> lightResult =
            lightObject->AddComponentWithoutRenderRegistration<ve::LightComponent>();
        passed &= Expect(lightResult.IsOk(), "LightComponent should be added");
        ve::LightComponent* light = lightResult.GetValue();
        light->SetLightType(ve::LightType::Point);
        light->SetColor(ve::Vector3(1.0f, 0.75f, 0.5f));
        light->SetIntensity(3.0f);
        light->SetRange(12.0f);

        ve::Result<std::string> serialized = ve::SceneSerialization::SaveToString(sourceScene);
        passed &= Expect(serialized.IsOk(), "Scene should serialize to JSON text");
        passed &= Expect(serialized.GetValue().find("\n    \"rootGameObjects\"") != std::string::npos,
                         "Scene JSON should be pretty-printed");
        passed &= Expect(serialized.GetValue().find("\"localPosition\": [1E0, 2E0, 3E0]") != std::string::npos,
                         "Scene vector JSON should stay on one line");

        ve::Scene loadedScene;
        passed &= ExpectOk(ve::SceneSerialization::LoadFromString(loadedScene, serialized.GetValue()),
                           "Scene should deserialize from JSON text");
        passed &= Expect(loadedScene.GetName() == "Serialization Smoke", "Scene name should round-trip");
        passed &= Expect(loadedScene.GetRootGameObjectCount() == 2, "Root object count should round-trip");

        ve::GameObject* loadedRoot = loadedScene.GetRootGameObject(0);
        passed &= Expect(loadedRoot != nullptr && loadedRoot->GetName() == "Root", "Root object name should round-trip");
        ve::TransformComponent* loadedRootTransform = loadedRoot->GetComponent<ve::TransformComponent>();
        passed &= Expect(loadedRootTransform->GetLocalPosition().IsNearlyEqual(ve::Vector3(1.0f, 2.0f, 3.0f)),
                         "Root position should round-trip");
        ve::MeshRenderComponent* loadedMesh = loadedRoot->GetComponent<ve::MeshRenderComponent>();
        passed &= Expect(loadedMesh != nullptr, "Mesh component should round-trip");
        passed &= Expect(loadedMesh->GetMeshAssetGuid() == "11111111-1111-1111-1111-111111111111",
                         "Mesh GUID should round-trip");

        ve::GameObject* loadedCameraObject = loadedRootTransform->GetChildGameObject(0);
        passed &= Expect(loadedCameraObject != nullptr && loadedCameraObject->GetName() == "Camera",
                         "Child object should round-trip");
        ve::CameraComponent* loadedCamera = loadedCameraObject->GetComponent<ve::CameraComponent>();
        passed &= Expect(loadedCamera != nullptr && loadedCamera->IsPrimary(), "Camera primary flag should round-trip");
        passed &= Expect(loadedCamera->GetProjectionMode() == ve::CameraComponent::ProjectionMode::Orthographic,
                         "Camera projection mode should round-trip");

        ve::GameObject* loadedLightObject = loadedScene.GetRootGameObject(1);
        ve::LightComponent* loadedLight = loadedLightObject->GetComponent<ve::LightComponent>();
        passed &= Expect(loadedLight != nullptr, "Light component should round-trip");
        passed &= Expect(loadedLight->GetLightType() == ve::LightType::Point, "Light type should round-trip");
        passed &= Expect(ve::NearlyEqual(loadedLight->GetIntensity(), 3.0f), "Light intensity should round-trip");

        return passed;
    }

} // namespace

int main()
{
    bool passed = true;

    passed &= TestSceneRoundTrip();

    if (passed)
    {
        std::cout << "VEngineSceneSerializationTests passed" << '\n';
        return 0;
    }

    return 1;
}
