#include "Engine/Runtime/Scene/Scene.h"

#include <algorithm>

namespace ve
{
    Scene::Scene() = default;

    Scene::Scene(std::string name)
        : name_(std::move(name))
    {
    }

    Scene::~Scene() = default;

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

    Result<GameObject*> Scene::CreateRootGameObject(std::string name)
    {
        try
        {
            std::unique_ptr<GameObject> gameObject = std::make_unique<GameObject>(std::move(name));
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
                               [&gameObject](const std::unique_ptr<GameObject>& candidate)
                               { return candidate.get() == &gameObject; });

        if (it == rootGameObjects_.end())
        {
            return false;
        }

        rootGameObjects_.erase(it);
        return true;
    }

    void Scene::Clear() noexcept
    {
        rootGameObjects_.clear();
    }

    void Scene::Update(Float32 deltaSeconds)
    {
        for (const std::unique_ptr<GameObject>& gameObject : rootGameObjects_)
        {
            gameObject->Update(deltaSeconds);
        }
    }
} // namespace ve
