#include "Engine/Runtime/Scene/GameObject.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Physics/ColliderComponent.h"
#include "Engine/Runtime/Physics/RigidBodyComponent.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/TransformComponent.h"

#include <algorithm>
#include <utility>

namespace ve
{
    GameObject::GameObject(Scene& scene, SceneObjectId id, std::string name)
        : scene_(&scene)
        , id_(id)
        , name_(std::move(name))
    {
    }

    GameObject::~GameObject()
    {
        DestroyComponents();
    }

    SceneObjectId GameObject::GetId() const noexcept
    {
        return id_;
    }

    const std::string& GameObject::GetName() const noexcept
    {
        return name_;
    }

    void GameObject::SetName(std::string name)
    {
        GetScene().ValidateMutationAccess();
        name_ = std::move(name);
    }

    bool GameObject::IsActiveSelf() const noexcept
    {
        return activeSelf_;
    }

    bool GameObject::IsActiveInHierarchy() const noexcept
    {
        return activeInHierarchy_;
    }

    void GameObject::SetActive(bool active)
    {
        GetScene().ValidateMutationAccess();
        if (activeSelf_ == active)
        {
            return;
        }

        activeSelf_ = active;
        RefreshActiveInHierarchy();
        MarkTransformHierarchyDirty();
    }

    Scene& GameObject::GetScene() noexcept
    {
        VE_ASSERT_MESSAGE(scene_ != nullptr, "GameObject is not attached to a Scene.");
        return *scene_;
    }

    const Scene& GameObject::GetScene() const noexcept
    {
        VE_ASSERT_MESSAGE(scene_ != nullptr, "GameObject is not attached to a Scene.");
        return *scene_;
    }

    GameObject* GameObject::GetParent() noexcept
    {
        return parent_;
    }

    const GameObject* GameObject::GetParent() const noexcept
    {
        return parent_;
    }

    const std::vector<GameObject*>& GameObject::GetChildren() const noexcept
    {
        return children_;
    }

    void GameObject::SetParent(GameObject* parent)
    {
        GetScene().ValidateMutationAccess();
        if (parent_ == parent)
        {
            return;
        }

        if (parent != nullptr)
        {
            if (parent->scene_ != scene_)
            {
                VE_ASSERT_MESSAGE(false, "GameObject parent must belong to the same Scene.");
                return;
            }

            if (parent == this)
            {
                VE_ASSERT_MESSAGE(false, "GameObject cannot be parented to itself.");
                return;
            }

            if (IsAncestorOf(*parent))
            {
                VE_ASSERT_MESSAGE(false, "GameObject cannot be parented to one of its descendants.");
                return;
            }
        }

        DetachFromParent();
        if (parent == nullptr)
        {
            GetScene().AddRootGameObject(*this);
        }
        else
        {
            AttachToParent(*parent);
        }

        RefreshActiveInHierarchy();
    }

    const std::vector<std::unique_ptr<Component>>& GameObject::GetComponents() const noexcept
    {
        return components_;
    }

    bool GameObject::RemoveComponent(Component& component)
    {
        GetScene().ValidateMutationAccess();
        auto iter =
            std::find_if(components_.begin(),
                         components_.end(),
                         [&component](const std::unique_ptr<Component>& item) { return item.get() == &component; });
        if (iter == components_.end())
        {
            return false;
        }

        (*iter)->DispatchDisable();
        (*iter)->DispatchDestroy();
        (*iter)->DetachFromGameObject();
        components_.erase(iter);
        return true;
    }

    Component& GameObject::AddComponent(std::unique_ptr<Component> component)
    {
        GetScene().ValidateMutationAccess();
        VE_ASSERT_MESSAGE(component != nullptr, "Cannot add a null Component.");

        if (dynamic_cast<ColliderComponent*>(component.get()) != nullptr)
        {
            if (ColliderComponent* existingCollider = GetComponent<ColliderComponent>())
            {
                return *existingCollider;
            }
        }
        if (dynamic_cast<RigidBodyComponent*>(component.get()) != nullptr)
        {
            if (RigidBodyComponent* existingRigidBody = GetComponent<RigidBodyComponent>())
            {
                return *existingRigidBody;
            }
        }

        Component& componentRef = *component;
        componentRef.AttachToGameObject(*this);
        components_.push_back(std::move(component));
        componentRef.DispatchCreate();
        RefreshComponentsActiveState();
        MarkTransformHierarchyDirty();
        return componentRef;
    }

    void GameObject::DestroyComponents() noexcept
    {
        for (std::unique_ptr<Component>& component : components_)
        {
            component->DispatchDisable();
            component->DispatchDestroy();
            component->DetachFromGameObject();
        }

        components_.clear();
    }

    void GameObject::RefreshActiveInHierarchy()
    {
        const bool parentActive = parent_ == nullptr || parent_->IsActiveInHierarchy();
        const bool nextActiveInHierarchy = activeSelf_ && parentActive;
        const bool changed = activeInHierarchy_ != nextActiveInHierarchy;
        activeInHierarchy_ = nextActiveInHierarchy;

        if (changed)
        {
            RefreshComponentsActiveState();
        }

        for (GameObject* child : children_)
        {
            child->RefreshActiveInHierarchy();
        }
    }

    void GameObject::RefreshComponentsActiveState()
    {
        for (std::unique_ptr<Component>& component : components_)
        {
            if (component->IsEnabled() && activeInHierarchy_)
            {
                component->DispatchEnable();
            }
            else
            {
                component->DispatchDisable();
            }
        }
    }

    void GameObject::MarkTransformHierarchyDirty()
    {
        if (TransformComponent* transform = GetComponent<TransformComponent>())
        {
            transform->dirty_ = true;
        }

        for (GameObject* child : children_)
        {
            child->MarkTransformHierarchyDirty();
        }
    }

    void GameObject::DetachFromParent() noexcept
    {
        if (parent_ != nullptr)
        {
            std::vector<GameObject*>& siblings = parent_->children_;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
            parent_ = nullptr;
            return;
        }

        if (scene_ != nullptr)
        {
            scene_->RemoveRootGameObject(*this);
        }
    }

    void GameObject::AttachToParent(GameObject& parent)
    {
        GetScene().RemoveRootGameObject(*this);
        parent_ = &parent;
        parent.children_.push_back(this);
    }

    bool GameObject::IsAncestorOf(const GameObject& object) const noexcept
    {
        const GameObject* current = object.parent_;
        while (current != nullptr)
        {
            if (current == this)
            {
                return true;
            }

            current = current->parent_;
        }

        return false;
    }
} // namespace ve
