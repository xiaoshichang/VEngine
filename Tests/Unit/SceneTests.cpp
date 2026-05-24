#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/RenderComponents.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneRenderExtractor.h"
#include "Engine/Runtime/Scene/Serialization/SceneSerialization.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <iostream>
#include <string>
#include <vector>

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

    struct LifecycleLog
    {
        int createCount = 0;
        int destroyCount = 0;
        int enableCount = 0;
        int disableCount = 0;
        int updateCount = 0;
        std::vector<std::string> events;
    };

    class TestComponent final : public ve::Component
    {
    public:
        explicit TestComponent(LifecycleLog& log)
            : log_(&log)
        {
        }

    protected:
        void OnCreate() override
        {
            ++log_->createCount;
            log_->events.push_back("create");
        }

        void OnDestroy() override
        {
            ++log_->destroyCount;
            log_->events.push_back("destroy");
        }

        void OnEnable() override
        {
            ++log_->enableCount;
            log_->events.push_back("enable");
        }

        void OnDisable() override
        {
            ++log_->disableCount;
            log_->events.push_back("disable");
        }

        void OnUpdate() override
        {
            ++log_->updateCount;
            log_->events.push_back("update");
        }

    private:
        LifecycleLog* log_ = nullptr;
    };

    bool TestCreateFindAndName()
    {
        bool passed = true;

        ve::Scene scene;
        ve::GameObject& gameObject = scene.CreateGameObject("Player");

        passed &= Expect(gameObject.GetId() != ve::InvalidSceneObjectId, "GameObject should receive a stable id");
        passed &= Expect(gameObject.GetName() == "Player", "GameObject should store its name");
        passed &= Expect(scene.FindGameObject(gameObject.GetId()) == &gameObject, "Scene should find GameObject by id");
        passed &= Expect(scene.GetGameObjectCount() == 1, "Scene should track GameObject count");
        passed &= Expect(scene.GetRootGameObjects().size() == 1, "New GameObject should start as a root object");
        passed &= Expect(scene.GetRootGameObjects()[0] == &gameObject, "Root list should contain created GameObject");

        gameObject.SetName("Hero");
        passed &= Expect(gameObject.GetName() == "Hero", "GameObject name should be mutable");

        return passed;
    }

    bool TestHierarchyOwnershipAndReparent()
    {
        bool passed = true;

        ve::Scene scene;
        ve::GameObject& parent = scene.CreateGameObject("Parent");
        ve::GameObject& child = scene.CreateGameObject("Child");
        ve::GameObject& otherParent = scene.CreateGameObject("OtherParent");

        child.SetParent(&parent);
        passed &= Expect(child.GetParent() == &parent, "Child should store parent pointer after SetParent");
        passed &= Expect(parent.GetChildren().size() == 1, "Parent should include child");
        passed &= Expect(parent.GetChildren()[0] == &child, "Parent child list should contain child");
        passed &= Expect(scene.GetRootGameObjects().size() == 2, "Parented child should leave root list");

        child.SetParent(&otherParent);
        passed &= Expect(child.GetParent() == &otherParent, "Child should reparent to new parent");
        passed &= Expect(parent.GetChildren().empty(), "Old parent should no longer contain child");
        passed &= Expect(otherParent.GetChildren().size() == 1, "New parent should contain child");

        child.SetParent(nullptr);
        passed &= Expect(child.GetParent() == nullptr, "Child should detach to root");
        passed &= Expect(otherParent.GetChildren().empty(), "Detached child should leave old parent children");
        passed &= Expect(scene.GetRootGameObjects().size() == 3, "Detached child should return to root list");

        return passed;
    }

    bool TestComponentLifecycleAndUpdate()
    {
        bool passed = true;

        ve::Scene scene;
        ve::GameObject& gameObject = scene.CreateGameObject("Object");
        LifecycleLog log;
        TestComponent& component = gameObject.AddComponent<TestComponent>(log);

        passed &= Expect(log.createCount == 1, "AddComponent should call OnCreate");
        passed &= Expect(log.enableCount == 1, "AddComponent on active object should call OnEnable");
        passed &= Expect(component.IsActiveAndEnabled(), "Component should be active and enabled after add");
        passed &=
            Expect(gameObject.GetComponent<TestComponent>() == &component, "GetComponent should return component");

        scene.Update();
        passed &= Expect(log.updateCount == 1, "Scene::Update should call OnUpdate on active enabled component");

        component.SetEnabled(false);
        passed &= Expect(log.disableCount == 1, "Disabling component should call OnDisable");
        passed &= Expect(!component.IsActiveAndEnabled(), "Disabled component should not be active and enabled");

        scene.Update();
        passed &= Expect(log.updateCount == 1, "Disabled component should not update");

        component.SetEnabled(true);
        passed &= Expect(log.enableCount == 2, "Re-enabling component should call OnEnable");

        passed &= Expect(gameObject.RemoveComponent(component), "RemoveComponent should remove an owned component");
        passed &= Expect(log.disableCount == 2, "Removing active component should call OnDisable");
        passed &= Expect(log.destroyCount == 1, "RemoveComponent should call OnDestroy");
        passed &= Expect(gameObject.GetComponent<TestComponent>() == nullptr, "Removed component should not be found");

        return passed;
    }

    bool TestActiveStatePropagatesThroughHierarchy()
    {
        bool passed = true;

        ve::Scene scene;
        ve::GameObject& parent = scene.CreateGameObject("Parent");
        ve::GameObject& child = scene.CreateGameObject("Child");
        child.SetParent(&parent);

        LifecycleLog log;
        TestComponent& component = child.AddComponent<TestComponent>(log);
        passed &= Expect(component.IsActiveAndEnabled(), "Child component should start active and enabled");

        parent.SetActive(false);
        passed &= Expect(!parent.IsActiveInHierarchy(), "Inactive parent should not be active in hierarchy");
        passed &= Expect(child.IsActiveSelf(), "Child activeSelf should stay true when parent is inactive");
        passed &= Expect(!child.IsActiveInHierarchy(), "Child should become inactive in hierarchy");
        passed &= Expect(log.disableCount == 1, "Parent deactivation should disable active child component");

        scene.Update();
        passed &= Expect(log.updateCount == 0, "Inactive hierarchy component should not update");

        parent.SetActive(true);
        passed &= Expect(child.IsActiveInHierarchy(), "Child should become active when parent reactivates");
        passed &= Expect(log.enableCount == 2, "Parent reactivation should enable child component again");

        return passed;
    }

    bool TestDestroyGameObjectTree()
    {
        bool passed = true;

        ve::Scene scene;
        ve::GameObject& parent = scene.CreateGameObject("Parent");
        ve::GameObject& child = scene.CreateGameObject("Child");
        child.SetParent(&parent);
        const ve::SceneObjectId parentId = parent.GetId();
        const ve::SceneObjectId childId = child.GetId();

        LifecycleLog parentLog;
        LifecycleLog childLog;
        parent.AddComponent<TestComponent>(parentLog);
        child.AddComponent<TestComponent>(childLog);

        passed &= Expect(scene.DestroyGameObject(parent), "DestroyGameObject should destroy objects owned by scene");
        passed &= Expect(scene.GetGameObjectCount() == 0, "Destroying parent should remove entire subtree");
        passed &= Expect(scene.FindGameObject(parentId) == nullptr, "Destroyed parent should not be found");
        passed &= Expect(scene.FindGameObject(childId) == nullptr, "Destroyed child should not be found");
        passed &= Expect(scene.GetRootGameObjects().empty(), "Destroying subtree should clean root list");
        passed &= Expect(parentLog.disableCount == 1, "Destroying active parent component should call OnDisable");
        passed &= Expect(parentLog.destroyCount == 1, "Destroying parent component should call OnDestroy");
        passed &= Expect(childLog.disableCount == 1, "Destroying active child component should call OnDisable");
        passed &= Expect(childLog.destroyCount == 1, "Destroying child component should call OnDestroy");

        return passed;
    }

    bool TestTransformHierarchyUpdatesWorldMatrices()
    {
        bool passed = true;

        ve::Scene scene;
        ve::GameObject& parent = scene.CreateGameObject("Parent");
        ve::GameObject& child = scene.CreateGameObject("Child");
        child.SetParent(&parent);

        ve::TransformComponent& parentTransform = parent.AddComponent<ve::TransformComponent>();
        ve::TransformComponent& childTransform = child.AddComponent<ve::TransformComponent>();
        parentTransform.SetLocalPosition(ve::Vector3(1.0f, 2.0f, 3.0f));
        childTransform.SetLocalPosition(ve::Vector3(4.0f, 0.0f, 0.0f));

        scene.UpdateTransforms();

        passed &= Expect(parentTransform.GetWorldPosition().IsNearlyEqual(ve::Vector3(1.0f, 2.0f, 3.0f)),
                         "Parent world position should match local position");
        passed &= Expect(childTransform.GetWorldPosition().IsNearlyEqual(ve::Vector3(5.0f, 2.0f, 3.0f)),
                         "Child world position should include parent transform");

        parentTransform.SetLocalPosition(ve::Vector3(2.0f, 2.0f, 3.0f));
        passed &= Expect(childTransform.IsDirty(), "Parent transform changes should mark child transform dirty");
        scene.UpdateTransforms();
        passed &= Expect(childTransform.GetWorldPosition().IsNearlyEqual(ve::Vector3(6.0f, 2.0f, 3.0f)),
                         "Child world position should update after parent transform changes");

        return passed;
    }

    bool TestSceneRenderSnapshotExtraction()
    {
        bool passed = true;

        ve::ResourceManager resourceManager;
        ve::Scene scene;

        ve::GameObject& cameraObject = scene.CreateGameObject("Camera");
        cameraObject.AddComponent<ve::TransformComponent>().SetLocalPosition(ve::Vector3(0.0f, 0.0f, -4.0f));
        cameraObject.AddComponent<ve::CameraComponent>();

        ve::GameObject& lightObject = scene.CreateGameObject("Light");
        lightObject.AddComponent<ve::TransformComponent>();
        lightObject.AddComponent<ve::LightComponent>();

        ve::GameObject& cubeObject = scene.CreateGameObject("Cube");
        cubeObject.AddComponent<ve::TransformComponent>();
        ve::MeshRendererComponent& renderer = cubeObject.AddComponent<ve::MeshRendererComponent>();
        renderer.SetMesh(resourceManager.GetFallbackMesh());
        renderer.SetMaterial(resourceManager.GetDefaultMaterial());

        ve::SceneRenderSnapshot snapshot = ve::ExtractSceneRenderSnapshot(scene, resourceManager, 42);

        passed &= Expect(snapshot.frameId == 42, "Snapshot should preserve frame id");
        passed &= Expect(snapshot.hasMainCamera, "Snapshot should include a main camera");
        passed &= Expect(snapshot.directionalLights.size() == 1, "Snapshot should include one directional light");
        passed &= Expect(snapshot.drawItems.size() == 1, "Snapshot should include one draw item");
        passed &= Expect(!snapshot.drawItems[0].vertices.empty(), "Snapshot draw item should contain render vertices");
        passed &= Expect(snapshot.drawItems[0].objectId == cubeObject.GetId(),
                         "Snapshot draw item should use stable object id");

        return passed;
    }

    bool TestDiagonalSampleCameraSeesCube()
    {
        bool passed = true;

        ve::ResourceManager resourceManager;
        ve::Scene scene;

        ve::GameObject& cameraObject = scene.CreateGameObject("Camera");
        ve::TransformComponent& cameraTransform = cameraObject.AddComponent<ve::TransformComponent>();
        cameraTransform.SetLocalPosition(ve::Vector3(2.0f, 1.6f, -3.5f));
        cameraTransform.SetLocalRotation(
            ve::Quaternion::FromEulerXYZ(ve::ToRadians(20.0f), ve::ToRadians(-30.0f), 0.0f));
        cameraObject.AddComponent<ve::CameraComponent>();

        ve::GameObject& cubeObject = scene.CreateGameObject("Cube");
        cubeObject.AddComponent<ve::TransformComponent>();
        ve::MeshRendererComponent& renderer = cubeObject.AddComponent<ve::MeshRendererComponent>();
        renderer.SetMesh(resourceManager.GetFallbackMesh());
        renderer.SetMaterial(resourceManager.GetDefaultMaterial());

        const ve::SceneRenderSnapshot snapshot = ve::ExtractSceneRenderSnapshot(scene, resourceManager, 43);
        passed &= Expect(snapshot.drawItems.size() == 1, "Diagonal sample camera scene should include cube draw item");

        bool hasVisibleVertex = false;
        if (!snapshot.drawItems.empty())
        {
            for (const ve::SceneRenderVertex& vertex : snapshot.drawItems[0].vertices)
            {
                const ve::Vector3& position = vertex.position;
                if (position.GetX() >= -1.0f && position.GetX() <= 1.0f && position.GetY() >= -1.0f &&
                    position.GetY() <= 1.0f && position.GetZ() >= 0.0f && position.GetZ() <= 1.0f)
                {
                    hasVisibleVertex = true;
                    break;
                }
            }
        }

        passed &= Expect(hasVisibleVertex, "Diagonal sample camera should project at least one cube vertex on screen");

        return passed;
    }

    bool TestBuiltInCubeUsesFaceColors()
    {
        bool passed = true;

        ve::ResourceManager resourceManager;
        const ve::MeshResource* cube = resourceManager.FindMesh(resourceManager.GetFallbackMesh());

        passed &= Expect(cube != nullptr, "Built-in cube mesh should exist");
        if (cube == nullptr)
        {
            return passed;
        }

        passed &= Expect(cube->vertices.size() == 36, "Built-in cube should contain six non-indexed quads");

        std::vector<ve::Vector3> uniqueColors;
        for (const ve::MeshVertex& vertex : cube->vertices)
        {
            bool found = false;
            for (const ve::Vector3& color : uniqueColors)
            {
                if (vertex.color.IsNearlyEqual(color))
                {
                    found = true;
                    break;
                }
            }

            if (!found)
            {
                uniqueColors.push_back(vertex.color);
            }
        }

        passed &= Expect(uniqueColors.size() == 6, "Built-in cube should have one distinct color per face");

        return passed;
    }

    bool TestSceneSerializationRoundTrip()
    {
        bool passed = true;

        ve::ReflectionRegistry reflectionRegistry;
        ve::RegisterSceneReflectionTypes(reflectionRegistry);
        ve::ResourceManager resourceManager;

        ve::Scene sourceScene;
        ve::GameObject& camera = sourceScene.CreateGameObject("Camera");
        camera.AddComponent<ve::TransformComponent>().SetLocalPosition(ve::Vector3(0.0f, 1.0f, -4.0f));
        camera.AddComponent<ve::CameraComponent>();

        ve::GameObject& cube = sourceScene.CreateGameObject("Cube");
        cube.SetParent(&camera);
        cube.AddComponent<ve::TransformComponent>().SetLocalScale(ve::Vector3(2.0f, 2.0f, 2.0f));
        ve::MeshRendererComponent& renderer = cube.AddComponent<ve::MeshRendererComponent>();
        renderer.SetMesh(resourceManager.GetFallbackMesh());
        renderer.SetMaterial(resourceManager.GetDefaultMaterial());

        const std::string json = ve::SerializeSceneToJson(sourceScene, reflectionRegistry);

        ve::Scene loadedScene;
        passed &= Expect(ve::DeserializeSceneFromJson(loadedScene, reflectionRegistry, json) == ve::ErrorCode::None,
                         "Scene JSON should deserialize");
        passed &= Expect(loadedScene.GetGameObjectCount() == 2, "Loaded scene should preserve GameObject count");

        ve::GameObject* loadedCamera = loadedScene.FindGameObject(camera.GetId());
        ve::GameObject* loadedCube = loadedScene.FindGameObject(cube.GetId());
        passed &= Expect(loadedCamera != nullptr, "Loaded scene should preserve camera id");
        passed &= Expect(loadedCube != nullptr, "Loaded scene should preserve cube id");
        passed &= Expect(loadedCube != nullptr && loadedCube->GetParent() == loadedCamera,
                         "Loaded scene should preserve hierarchy");
        passed &= Expect(loadedCube != nullptr && loadedCube->GetComponent<ve::MeshRendererComponent>() != nullptr,
                         "Loaded scene should preserve MeshRenderer component");

        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestCreateFindAndName();
    passed &= TestHierarchyOwnershipAndReparent();
    passed &= TestComponentLifecycleAndUpdate();
    passed &= TestActiveStatePropagatesThroughHierarchy();
    passed &= TestDestroyGameObjectTree();
    passed &= TestTransformHierarchyUpdatesWorldMatrices();
    passed &= TestSceneRenderSnapshotExtraction();
    passed &= TestDiagonalSampleCameraSeesCube();
    passed &= TestBuiltInCubeUsesFaceColors();
    passed &= TestSceneSerializationRoundTrip();

    return passed ? 0 : 1;
}
