#include "Engine/Runtime/Scene/GameObject.h"

#include <algorithm>

namespace ve
{
    GameObject::GameObject() = default;

    GameObject::GameObject(std::string name)
        : name_(std::move(name))
    {
    }

    GameObject::~GameObject()
    {
        for (std::unique_ptr<Component>& component : components_)
        {
            component->SetOwner(nullptr);
        }
    }

    const std::string& GameObject::GetName() const noexcept
    {
        return name_;
    }

    void GameObject::SetName(std::string name)
    {
        name_ = std::move(name);
    }

    bool GameObject::IsActive() const noexcept
    {
        return active_;
    }

    void GameObject::SetActive(bool active) noexcept
    {
        active_ = active;
    }

    GameObject* GameObject::GetParent() noexcept
    {
        return parent_;
    }

    const GameObject* GameObject::GetParent() const noexcept
    {
        return parent_;
    }

    SizeT GameObject::GetChildCount() const noexcept
    {
        return children_.size();
    }

    GameObject* GameObject::GetChild(SizeT index) noexcept
    {
        if (index >= children_.size())
        {
            return nullptr;
        }

        return children_[index].get();
    }

    const GameObject* GameObject::GetChild(SizeT index) const noexcept
    {
        if (index >= children_.size())
        {
            return nullptr;
        }

        return children_[index].get();
    }

    Result<GameObject*> GameObject::CreateChild(std::string name)
    {
        try
        {
            std::unique_ptr<GameObject> child = std::make_unique<GameObject>(std::move(name));
            GameObject* childPointer = child.get();
            childPointer->SetParent(this);
            children_.push_back(std::move(child));
            return Result<GameObject*>::Success(childPointer);
        }
        catch (const std::bad_alloc&)
        {
            return Result<GameObject*>::Failure(Error(ErrorCode::OutOfMemory, "GameObject child allocation failed."));
        }
    }

    bool GameObject::DestroyChild(GameObject& child) noexcept
    {
        auto it =
            std::find_if(children_.begin(),
                         children_.end(),
                         [&child](const std::unique_ptr<GameObject>& candidate) { return candidate.get() == &child; });

        if (it == children_.end())
        {
            return false;
        }

        (*it)->SetParent(nullptr);
        children_.erase(it);
        return true;
    }

    void GameObject::ClearChildren() noexcept
    {
        for (std::unique_ptr<GameObject>& child : children_)
        {
            child->SetParent(nullptr);
        }

        children_.clear();
    }

    SizeT GameObject::GetComponentCount() const noexcept
    {
        return components_.size();
    }

    Component* GameObject::GetComponent(SizeT index) noexcept
    {
        if (index >= components_.size())
        {
            return nullptr;
        }

        return components_[index].get();
    }

    const Component* GameObject::GetComponent(SizeT index) const noexcept
    {
        if (index >= components_.size())
        {
            return nullptr;
        }

        return components_[index].get();
    }

    void GameObject::Update(Float32 deltaSeconds)
    {
        if (!active_)
        {
            return;
        }

        for (const std::unique_ptr<Component>& component : components_)
        {
            if (component->IsEnabled())
            {
                component->OnUpdate(deltaSeconds);
            }
        }

        for (const std::unique_ptr<GameObject>& child : children_)
        {
            child->Update(deltaSeconds);
        }
    }

    void GameObject::SetParent(GameObject* parent) noexcept
    {
        parent_ = parent;
    }
} // namespace ve
