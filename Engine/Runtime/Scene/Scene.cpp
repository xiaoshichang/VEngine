#include "Engine/Runtime/Scene/Scene.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Scene/SceneSystem.h"

#include <algorithm>
#include <utility>

namespace ve
{
    Scene::Scene()
        : rtScene_(std::make_shared<RTScene>())
    {
    }

    Scene::Scene(std::string name)
        : name_(std::move(name))
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

    CameraComponent* Scene::GetMainCamera() noexcept
    {
        CameraComponent* fallbackCamera = nullptr;
        for (const std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            if (gameObject == nullptr)
            {
                continue;
            }

            CameraComponent* camera = FindMainCameraRecursive(*gameObject);
            if (camera != nullptr && camera->IsPrimary())
            {
                return camera;
            }

            if (fallbackCamera == nullptr)
            {
                fallbackCamera = camera;
            }
        }

        return fallbackCamera;
    }

    const CameraComponent* Scene::GetMainCamera() const noexcept
    {
        const CameraComponent* fallbackCamera = nullptr;
        for (const std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            if (gameObject == nullptr)
            {
                continue;
            }

            const CameraComponent* camera = FindMainCameraRecursive(*gameObject);
            if (camera != nullptr && camera->IsPrimary())
            {
                return camera;
            }

            if (fallbackCamera == nullptr)
            {
                fallbackCamera = camera;
            }
        }

        return fallbackCamera;
    }

    Result<GameObject*> Scene::CreateRootGameObject(std::string name)
    {
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

    bool Scene::DestroyRootGameObject(GameObject& gameObject) noexcept
    {
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

        if (sceneSystem_ == nullptr)
        {
            return;
        }

        if (!hadRootGameObjects)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneClear", [rtScene]() { rtScene->Clear(); });
    }

    void Scene::SetSceneSystem(SceneSystem* sceneSystem) noexcept
    {
        sceneSystem_ = sceneSystem;
        if (sceneSystem_ != nullptr)
        {
            RebuildRenderThreadScene();
        }
    }

    std::shared_ptr<RTScene> Scene::GetRTScene() noexcept
    {
        return rtScene_;
    }

    std::shared_ptr<const RTScene> Scene::GetRTScene() const noexcept
    {
        return rtScene_;
    }

    void Scene::RegisterRenderItem(std::shared_ptr<RTRenderItem> item)
    {
        if (item == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneAddRenderItem", [rtScene, item = std::move(item)]() { rtScene->AddRenderItem(item); });
    }

    void Scene::UnregisterRenderItem(std::shared_ptr<RTRenderItem> item) noexcept
    {
        if (item == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneRemoveRenderItem", [rtScene, item = std::move(item)]() { rtScene->RemoveRenderItem(item); });
    }

    void Scene::UpdateRenderItem(std::shared_ptr<RTRenderItem> item, RTRenderItemUpdateParam updateParam)
    {
        if (item == nullptr)
        {
            return;
        }

        SubmitRTSceneCommand(
            "RTSceneUpdateRenderItem", [item = std::move(item), updateParam = std::move(updateParam)]() mutable { item->ApplyUpdateParam(std::move(updateParam)); });
    }

    void Scene::RegisterCamera(std::shared_ptr<RTCamera> camera)
    {
        if (camera == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneAddCamera", [rtScene, camera = std::move(camera)]() { rtScene->AddCamera(camera); });
    }

    void Scene::UnregisterCamera(std::shared_ptr<RTCamera> camera) noexcept
    {
        if (camera == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneRemoveCamera", [rtScene, camera = std::move(camera)]() { rtScene->RemoveCamera(camera); });
    }

    void Scene::UpdateCamera(std::shared_ptr<RTCamera> camera, RTCameraUpdateParam updateParam)
    {
        if (camera == nullptr)
        {
            return;
        }

        SubmitRTSceneCommand(
            "RTSceneUpdateCamera", [camera = std::move(camera), updateParam = std::move(updateParam)]() mutable { camera->ApplyUpdateParam(std::move(updateParam)); });
    }

    void Scene::RegisterLight(std::shared_ptr<RTLight> light)
    {
        if (light == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneAddLight", [rtScene, light = std::move(light)]() { rtScene->AddLight(light); });
    }

    void Scene::UnregisterLight(std::shared_ptr<RTLight> light) noexcept
    {
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

        SubmitRTSceneCommand(
            "RTSceneUpdateLight", [light = std::move(light), updateParam = std::move(updateParam)]() mutable { light->ApplyUpdateParam(std::move(updateParam)); });
    }

    void Scene::Update(Float32 deltaSeconds)
    {
        for (const std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            gameObject->Update(deltaSeconds);
        }
    }

    Result<GameObject*> Scene::CreateRootGameObjectWithoutRenderRegistration(std::string name)
    {
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

    void Scene::LateUpdate(Float32 deltaSeconds)
    {
        for (const std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            gameObject->LateUpdate(deltaSeconds);
        }
    }

    void Scene::BeforeRender()
    {
        for (std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            SyncRenderItemsBeforeRenderRecursive(*gameObject);
        }
    }

    void Scene::RebuildRenderThreadScene()
    {
        if (sceneSystem_ == nullptr)
        {
            return;
        }

        std::shared_ptr<RTScene> rtScene = rtScene_;
        SubmitRTSceneCommand("RTSceneClear", [rtScene]() { rtScene->Clear(); });

        for (std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            RegisterRenderItemsRecursive(*gameObject);
        }
    }

    void Scene::RegisterRenderItemsRecursive(GameObject& gameObject)
    {
        if (MeshRenderComponent* mesh = gameObject.GetComponent<MeshRenderComponent>(); mesh != nullptr)
        {
            mesh->RegisterRenderItemToRenderThread();
        }

        if (CameraComponent* camera = gameObject.GetComponent<CameraComponent>(); camera != nullptr)
        {
            camera->RegisterCameraToRenderThread();
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
        if (!function)
        {
            return;
        }

        if (sceneSystem_ == nullptr || !sceneSystem_->HasRenderSystem())
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Scene RTScene commands require a SceneSystem with an initialized RenderSystem.");
            return;
        }

        sceneSystem_->EnqueueRenderCommand(RenderCommand{std::move(debugName), std::move(function)});
    }

    CameraComponent* Scene::FindMainCameraRecursive(GameObject& gameObject) noexcept
    {
        CameraComponent* fallbackCamera = gameObject.GetComponent<CameraComponent>();
        if (fallbackCamera != nullptr && fallbackCamera->IsPrimary())
        {
            return fallbackCamera;
        }

        TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
        if (transform == nullptr)
        {
            return fallbackCamera;
        }

        for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
        {
            GameObject* child = transform->GetChildGameObject(childIndex);
            if (child == nullptr)
            {
                continue;
            }

            CameraComponent* childCamera = FindMainCameraRecursive(*child);
            if (childCamera != nullptr && childCamera->IsPrimary())
            {
                return childCamera;
            }

            if (fallbackCamera == nullptr)
            {
                fallbackCamera = childCamera;
            }
        }

        return fallbackCamera;
    }

    const CameraComponent* Scene::FindMainCameraRecursive(const GameObject& gameObject) noexcept
    {
        const CameraComponent* fallbackCamera = gameObject.GetComponent<CameraComponent>();
        if (fallbackCamera != nullptr && fallbackCamera->IsPrimary())
        {
            return fallbackCamera;
        }

        const TransformComponent* transform = gameObject.GetComponent<TransformComponent>();
        if (transform == nullptr)
        {
            return fallbackCamera;
        }

        for (SizeT childIndex = 0; childIndex < transform->GetChildCount(); ++childIndex)
        {
            const GameObject* child = transform->GetChildGameObject(childIndex);
            if (child == nullptr)
            {
                continue;
            }

            const CameraComponent* childCamera = FindMainCameraRecursive(*child);
            if (childCamera != nullptr && childCamera->IsPrimary())
            {
                return childCamera;
            }

            if (fallbackCamera == nullptr)
            {
                fallbackCamera = childCamera;
            }
        }

        return fallbackCamera;
    }
} // namespace ve
