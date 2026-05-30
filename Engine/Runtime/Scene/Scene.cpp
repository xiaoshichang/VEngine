#include "Engine/Runtime/Scene/Scene.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/GameThread/GameThreadSystem.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <algorithm>
#include <utility>

namespace ve
{
    Scene::Scene(const SceneDesc& desc)
        : gameThreadSystem_(desc.gameThreadSystem)
        , scriptContext_(desc.scriptContext)
    {
    }

    Scene::~Scene()
    {
        Clear();
    }

    GameObject& Scene::CreateGameObject(std::string name)
    {
        ValidateMutationAccess();

        auto gameObject = std::unique_ptr<GameObject>(new GameObject(*this, AllocateObjectId(), std::move(name)));
        GameObject& gameObjectRef = *gameObject;
        gameObjects_.push_back(std::move(gameObject));
        RegisterGameObject(gameObjectRef);
        AddRootGameObject(gameObjectRef);
        return gameObjectRef;
    }

    GameObject& Scene::CreateGameObjectWithId(SceneObjectId id, std::string name)
    {
        ValidateMutationAccess();
        VE_ASSERT_MESSAGE(id != InvalidSceneObjectId, "Scene object id must be valid.");
        VE_ASSERT_MESSAGE(FindGameObject(id) == nullptr, "Scene object id must be unique.");

        auto gameObject = std::unique_ptr<GameObject>(new GameObject(*this, id, std::move(name)));
        GameObject& gameObjectRef = *gameObject;
        gameObjects_.push_back(std::move(gameObject));
        RegisterGameObject(gameObjectRef);
        AddRootGameObject(gameObjectRef);
        nextObjectId_ = std::max(nextObjectId_, id + 1);
        return gameObjectRef;
    }

    bool Scene::DestroyGameObject(GameObject& gameObject) noexcept
    {
        ValidateMutationAccess();
        if (gameObject.scene_ != this)
        {
            return false;
        }

        DestroyGameObjectTree(gameObject);
        return true;
    }

    void Scene::Clear() noexcept
    {
        ValidateMutationAccess();
        while (!rootGameObjects_.empty())
        {
            DestroyGameObjectTree(*rootGameObjects_.back());
        }
    }

    GameObject* Scene::FindGameObject(SceneObjectId id) noexcept
    {
        const auto iter = gameObjectLookup_.find(id);
        return iter == gameObjectLookup_.end() ? nullptr : iter->second;
    }

    const GameObject* Scene::FindGameObject(SceneObjectId id) const noexcept
    {
        const auto iter = gameObjectLookup_.find(id);
        return iter == gameObjectLookup_.end() ? nullptr : iter->second;
    }

    const std::vector<GameObject*>& Scene::GetRootGameObjects() const noexcept
    {
        return rootGameObjects_;
    }

    SizeT Scene::GetGameObjectCount() const noexcept
    {
        return gameObjectLookup_.size();
    }

    void Scene::Update()
    {
        ValidateMutationAccess();

        std::vector<GameObject*> roots = rootGameObjects_;
        for (GameObject* root : roots)
        {
            if (FindGameObject(root->GetId()) != nullptr)
            {
                UpdateGameObject(*root);
            }
        }
    }

    void Scene::FixedUpdate(Float32 fixedDeltaSeconds)
    {
        ValidateMutationAccess();

        std::vector<GameObject*> roots = rootGameObjects_;
        for (GameObject* root : roots)
        {
            if (FindGameObject(root->GetId()) != nullptr)
            {
                FixedUpdateGameObject(*root, fixedDeltaSeconds);
            }
        }
    }

    void Scene::LateUpdate()
    {
        ValidateMutationAccess();

        std::vector<GameObject*> roots = rootGameObjects_;
        for (GameObject* root : roots)
        {
            if (FindGameObject(root->GetId()) != nullptr)
            {
                LateUpdateGameObject(*root);
            }
        }
    }

    void Scene::UpdateTransforms()
    {
        ValidateMutationAccess();

        std::vector<GameObject*> roots = rootGameObjects_;
        for (GameObject* root : roots)
        {
            if (FindGameObject(root->GetId()) != nullptr)
            {
                UpdateTransformTree(*root, Matrix44::Identity());
            }
        }
    }

    void Scene::ValidateMutationAccess() const noexcept
    {
        if (gameThreadSystem_ != nullptr)
        {
            gameThreadSystem_->ValidateGameThreadAccess();
        }
    }

    ScriptContext* Scene::GetScriptContext() noexcept
    {
        return scriptContext_;
    }

    const ScriptContext* Scene::GetScriptContext() const noexcept
    {
        return scriptContext_;
    }

    void Scene::SetScriptContext(ScriptContext* scriptContext)
    {
        ValidateMutationAccess();
        scriptContext_ = scriptContext;
    }

    SceneObjectId Scene::AllocateObjectId() noexcept
    {
        return nextObjectId_++;
    }

    void Scene::RegisterGameObject(GameObject& gameObject)
    {
        gameObjectLookup_[gameObject.GetId()] = &gameObject;
    }

    void Scene::UnregisterGameObject(GameObject& gameObject) noexcept
    {
        gameObjectLookup_.erase(gameObject.GetId());
    }

    void Scene::AddRootGameObject(GameObject& gameObject)
    {
        if (std::find(rootGameObjects_.begin(), rootGameObjects_.end(), &gameObject) == rootGameObjects_.end())
        {
            rootGameObjects_.push_back(&gameObject);
        }
    }

    void Scene::RemoveRootGameObject(GameObject& gameObject) noexcept
    {
        rootGameObjects_.erase(std::remove(rootGameObjects_.begin(), rootGameObjects_.end(), &gameObject),
                               rootGameObjects_.end());
    }

    void Scene::DestroyGameObjectTree(GameObject& gameObject) noexcept
    {
        std::vector<GameObject*> children = gameObject.children_;
        for (GameObject* child : children)
        {
            DestroyGameObjectTree(*child);
        }

        gameObject.DetachFromParent();
        gameObject.DestroyComponents();
        UnregisterGameObject(gameObject);
        gameObject.scene_ = nullptr;
        gameObject.parent_ = nullptr;
        gameObject.children_.clear();

        auto iter =
            std::find_if(gameObjects_.begin(),
                         gameObjects_.end(),
                         [&gameObject](const std::unique_ptr<GameObject>& item) { return item.get() == &gameObject; });
        if (iter != gameObjects_.end())
        {
            gameObjects_.erase(iter);
        }
    }

    void Scene::UpdateGameObject(GameObject& gameObject)
    {
        for (const std::unique_ptr<Component>& component : gameObject.components_)
        {
            component->DispatchUpdate();
        }

        std::vector<GameObject*> children = gameObject.children_;
        for (GameObject* child : children)
        {
            if (FindGameObject(child->GetId()) != nullptr)
            {
                UpdateGameObject(*child);
            }
        }
    }

    void Scene::FixedUpdateGameObject(GameObject& gameObject, Float32 fixedDeltaSeconds)
    {
        for (const std::unique_ptr<Component>& component : gameObject.components_)
        {
            component->DispatchFixedUpdate(fixedDeltaSeconds);
        }

        std::vector<GameObject*> children = gameObject.children_;
        for (GameObject* child : children)
        {
            if (FindGameObject(child->GetId()) != nullptr)
            {
                FixedUpdateGameObject(*child, fixedDeltaSeconds);
            }
        }
    }

    void Scene::LateUpdateGameObject(GameObject& gameObject)
    {
        for (const std::unique_ptr<Component>& component : gameObject.components_)
        {
            component->DispatchLateUpdate();
        }

        std::vector<GameObject*> children = gameObject.children_;
        for (GameObject* child : children)
        {
            if (FindGameObject(child->GetId()) != nullptr)
            {
                LateUpdateGameObject(*child);
            }
        }
    }

    void Scene::UpdateTransformTree(GameObject& gameObject, const Matrix44& parentWorld)
    {
        Matrix44 world = parentWorld;
        if (TransformComponent* transform = gameObject.GetComponent<TransformComponent>())
        {
            transform->UpdateWorldTransform(parentWorld);
            world = transform->GetWorldMatrix();
        }

        for (GameObject* child : gameObject.children_)
        {
            if (FindGameObject(child->GetId()) != nullptr)
            {
                UpdateTransformTree(*child, world);
            }
        }
    }
} // namespace ve
