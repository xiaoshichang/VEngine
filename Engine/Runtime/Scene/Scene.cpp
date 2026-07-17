#include "Engine/Runtime/Scene/Scene.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Scene/SceneSystem.h"

#include <algorithm>
#include <utility>

namespace ve
{
    Scene::Scene(SceneSystem& sceneSystem, SceneExecutionMode executionMode)
        : sceneSystem_(sceneSystem)
        , executionMode_(executionMode)
        , rtScene_(std::make_shared<RTScene>())
    {
    }

    Scene::Scene(SceneSystem& sceneSystem, SceneExecutionMode executionMode, std::string name)
        : name_(std::move(name))
        , sceneSystem_(sceneSystem)
        , executionMode_(executionMode)
        , rtScene_(std::make_shared<RTScene>())
    {
    }

    Scene::~Scene()
    {
        Clear();
    }

    const std::string& Scene::GetName() const noexcept
    {
        return name_;
    }

    void Scene::SetName(std::string name)
    {
        name_ = std::move(name);
    }

    SizeT Scene::GetRootGameObjectCount() const noexcept
    {
        return rootGameObjects_.size();
    }

    GameObject* Scene::GetRootGameObject(SizeT index) noexcept
    {
        VE_ASSERT_SCENE_THREAD();
        if (index >= rootGameObjects_.size())
        {
            return nullptr;
        }

        return rootGameObjects_[index].get();
    }

    const GameObject* Scene::GetRootGameObject(SizeT index) const noexcept
    {
        if (index >= rootGameObjects_.size())
        {
            return nullptr;
        }

        return rootGameObjects_[index].get();
    }

    CameraComponent* Scene::GetCamera() noexcept
    {
        VE_ASSERT_SCENE_THREAD();
        return cameras_.empty() ? nullptr : cameras_.front();
    }

    const CameraComponent* Scene::GetCamera() const noexcept
    {
        VE_ASSERT_SCENE_THREAD();
        return cameras_.empty() ? nullptr : cameras_.front();
    }

    Result<GameObject*> Scene::CreateRootGameObject(std::string name)
    {
        VE_ASSERT_SCENE_THREAD();
        try
        {
            std::unique_ptr<GameObject> gameObject = std::make_unique<GameObject>(*this, std::move(name));
            GameObject* gameObjectPointer = gameObject.get();
            rootGameObjects_.push_back(std::move(gameObject));
            return Result<GameObject*>::Success(gameObjectPointer);
        }
        catch (const std::bad_alloc&)
        {
            return Result<GameObject*>::Failure(Error(ErrorCode::OutOfMemory, "Scene GameObject allocation failed."));
        }
    }

    Result<GameObject*> Scene::CreateGameObject(std::string name, GameObject* parent)
    {
        VE_ASSERT_SCENE_THREAD();
        Result<GameObject*> gameObject = Result<GameObject*>::Failure(Error(ErrorCode::InvalidState, "Scene::CreateGameObject failed."));
        if (parent == nullptr)
        {
            gameObject = CreateRootGameObject(std::move(name));
        }
        else
        {
            if (parent->scene_ != this)
            {
                return Result<GameObject*>::Failure(Error(ErrorCode::InvalidArgument, "Parent GameObject does not belong to this scene."));
            }

            TransformComponent* parentTransform = parent->GetComponent<TransformComponent>();
            if (parentTransform == nullptr)
            {
                return Result<GameObject*>::Failure(Error(ErrorCode::InvalidArgument, "Parent GameObject is missing TransformComponent."));
            }

            gameObject = parentTransform->CreateChild(std::move(name));
        }

        if (gameObject && ShouldDispatchLifecycleCallbacks())
        {
            gameObject.GetValue()->DispatchLifecycleCreateCallbacksRecursive();
        }

        return gameObject;
    }

    bool Scene::DestroyRootGameObject(GameObject& gameObject) noexcept
    {
        VE_ASSERT_SCENE_THREAD();
        auto it = std::find_if(rootGameObjects_.begin(),
                               rootGameObjects_.end(),
                               [&gameObject](const std::unique_ptr<GameObject>& candidate) { return candidate.get() == &gameObject; });

        if (it == rootGameObjects_.end())
        {
            return false;
        }

        rootGameObjects_.erase(it);
        return true;
    }

    void Scene::Clear() noexcept
    {
        const bool hadRootGameObjects = !rootGameObjects_.empty();
        rootGameObjects_.clear();

        if (!hadRootGameObjects)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneClear", [rtScene]() { rtScene->Clear(); });
    }

    SceneSystem* Scene::GetSceneSystem() noexcept
    {
        return &sceneSystem_;
    }

    const SceneSystem* Scene::GetSceneSystem() const noexcept
    {
        return &sceneSystem_;
    }

    std::shared_ptr<RTScene> Scene::GetRTScene() noexcept
    {
        return rtScene_;
    }

    std::shared_ptr<const RTScene> Scene::GetRTScene() const noexcept
    {
        return rtScene_;
    }

    SceneExecutionMode Scene::GetExecutionMode() const noexcept
    {
        return executionMode_;
    }

    void Scene::RegisterRenderItem(std::shared_ptr<RTRenderItem> item)
    {
        VE_ASSERT_SCENE_THREAD();
        if (item == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneAddRenderItem", [rtScene, item = std::move(item)]() { rtScene->AddRenderItem(item); });
    }

    void Scene::UnregisterRenderItem(std::shared_ptr<RTRenderItem> item) noexcept
    {
        VE_ASSERT_SCENE_THREAD();
        if (item == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneRemoveRenderItem", [rtScene, item = std::move(item)]() { rtScene->RemoveRenderItem(item); });
    }

    void Scene::UpdateRenderItem(std::shared_ptr<RTRenderItem> item, RTRenderItemUpdateParam updateParam)
    {
        VE_ASSERT_SCENE_THREAD();
        if (item == nullptr)
        {
            return;
        }

        SubmitRTSceneCommand("RTSceneUpdateRenderItem",
                             [item = std::move(item), updateParam = std::move(updateParam)]() mutable { item->ApplyUpdateParam(std::move(updateParam)); });
    }

    void Scene::RegisterCamera(CameraComponent& camera)
    {
        VE_ASSERT_SCENE_THREAD();
        if (std::find(cameras_.begin(), cameras_.end(), &camera) == cameras_.end())
        {
            cameras_.push_back(&camera);
        }
    }

    void Scene::UnregisterCamera(CameraComponent& camera) noexcept
    {
        VE_ASSERT_SCENE_THREAD();
        cameras_.erase(std::remove(cameras_.begin(), cameras_.end(), &camera), cameras_.end());
    }

    void Scene::UpdateCamera(std::shared_ptr<RTCamera> camera, RTCameraUpdateParam updateParam)
    {
        VE_ASSERT_SCENE_THREAD();
        if (camera == nullptr)
        {
            return;
        }

        SubmitRTSceneCommand("RTSceneUpdateCamera",
                             [camera = std::move(camera), updateParam = std::move(updateParam)]() mutable
                             { camera->ApplyUpdateParam(std::move(updateParam)); });
    }

    void Scene::RegisterLight(std::shared_ptr<RTLight> light)
    {
        VE_ASSERT_SCENE_THREAD();
        if (light == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneAddLight", [rtScene, light = std::move(light)]() { rtScene->AddLight(light); });
    }

    void Scene::UnregisterLight(std::shared_ptr<RTLight> light) noexcept
    {
        VE_ASSERT_SCENE_THREAD();
        if (light == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneRemoveLight", [rtScene, light = std::move(light)]() { rtScene->RemoveLight(light); });
    }

    void Scene::UpdateLight(std::shared_ptr<RTLight> light, RTLightUpdateParam updateParam)
    {
        if (light == nullptr)
        {
            return;
        }

        SubmitRTSceneCommand("RTSceneUpdateLight",
                             [light = std::move(light), updateParam = std::move(updateParam)]() mutable { light->ApplyUpdateParam(std::move(updateParam)); });
    }

    void Scene::FixedUpdate(Float32 fixedDeltaSeconds)
    {
        VE_ASSERT_SCENE_THREAD();
        for (const std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            gameObject->FixedUpdate(fixedDeltaSeconds);
        }
    }

    void Scene::Update(Float32 deltaSeconds)
    {
        VE_ASSERT_SCENE_THREAD();
        for (const std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            gameObject->Update(deltaSeconds);
        }
    }

    void Scene::LateUpdate(Float32 deltaSeconds)
    {
        VE_ASSERT_SCENE_THREAD();
        for (const std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            gameObject->LateUpdate(deltaSeconds);
        }
    }

    void Scene::BeforeRender()
    {
        VE_ASSERT_SCENE_THREAD();
        for (std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            SyncRenderItemsBeforeRenderRecursive(*gameObject);
        }
    }

    void Scene::OnLoad()
    {
        VE_ASSERT_SCENE_THREAD();
        loaded_ = true;
        RebuildRenderThreadScene();

        if (!ShouldDispatchLifecycleCallbacks())
        {
            return;
        }

        const SizeT rootCount = GetRootGameObjectCount();
        for (SizeT rootIndex = 0; rootIndex < rootCount; ++rootIndex)
        {
            GameObject* root = GetRootGameObject(rootIndex);
            if (root != nullptr)
            {
                root->DispatchLifecycleCreateCallbacksRecursive();
            }
        }
    }

    void Scene::RebuildRenderThreadScene()
    {
        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneClear", [rtScene]() { rtScene->Clear(); });

        for (std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            RegisterRenderItemsRecursive(*gameObject);
        }
    }

    bool Scene::ShouldDispatchLifecycleCallbacks() const noexcept
    {
        return loaded_ && executionMode_ == SceneExecutionMode::Runtime;
    }

    void Scene::RegisterRenderItemsRecursive(GameObject& gameObject)
    {
        VE_ASSERT_SCENE_THREAD();
        if (MeshRenderComponent* mesh = gameObject.GetComponent<MeshRenderComponent>(); mesh != nullptr)
        {
            mesh->RegisterRenderItemToRenderThread();
        }

        if (CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr)
        {
            camera->RegisterCameraToScene();
        }

        if (LightComponent* light = gameObject.GetComponent<LightComponent>(); light != nullptr)
        {
            light->RegisterLightToRenderThread();
        }

        TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
        if (transform == nullptr)
        {
            return;
        }

        for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
        {
            GameObject* child = transform->GetChildGameObject(childIndex);
            if (child != nullptr)
            {
                RegisterRenderItemsRecursive(*child);
            }
        }
    }

    void Scene::SyncRenderItemsBeforeRenderRecursive(GameObject& gameObject)
    {
        VE_ASSERT_SCENE_THREAD();
        if (MeshRenderComponent* mesh = gameObject.GetComponent<MeshRenderComponent>(); mesh != nullptr)
        {
            mesh->SubmitRenderItemTransformUpdateToRenderThread();
        }

        if (CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr)
        {
            camera->SubmitCameraTransformUpdateToRenderThread();
        }

        if (LightComponent* light = gameObject.GetComponent<LightComponent>(); light != nullptr)
        {
            light->SubmitLightTransformUpdateToRenderThread();
        }

        TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
        if (transform != nullptr)
        {
            for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
            {
                GameObject* child = transform->GetChildGameObject(childIndex);
                if (child != nullptr)
                {
                    SyncRenderItemsBeforeRenderRecursive(*child);
                }
            }
        }
    }

    void Scene::SubmitRTSceneCommand(std::string debugName, std::function<void()> function) const
    {
        VE_ASSERT_SCENE_THREAD();
        if (!function)
        {
            return;
        }
        sceneSystem_.EnqueueRenderCommand(RenderCommand{std::move(debugName), std::move(function)});
    }

} // namespace ve
