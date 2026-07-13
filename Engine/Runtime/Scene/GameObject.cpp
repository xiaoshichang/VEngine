#include "Engine/Runtime/Scene/GameObject.h"

#include "Engine/Runtime/Scene/Scene.h"

#include <algorithm>

namespace ve
{
    GameObject::GameObject(Scene& scene)
        : scene_(&scene)
    {
        InitializeRequiredComponents();
    }

    GameObject::GameObject(Scene& scene, std::string name)
        : name_(std::move(name))
        , scene_(&scene)
    {
        InitializeRequiredComponents();
    }

    GameObject::~GameObject()
    {
        for (std::unique_ptr<ScriptableComponent>& script : scriptableComponents_)
        {
            if (script != nullptr)
            {
                DispatchComponentDestroyCallbacks(*script);
                script->ClearOwner();
            }
        }
        scriptableComponents_.clear();

        if (rigidbodyCmpt_ != nullptr)
        {
            DispatchComponentDestroyCallbacks(*rigidbodyCmpt_);
            rigidbodyCmpt_->ClearOwner();
        }

        if (colliderCmpt_ != nullptr)
        {
            DispatchComponentDestroyCallbacks(*colliderCmpt_);
            colliderCmpt_->ClearOwner();
        }

        if (lightCmpt_ != nullptr)
        {
            DispatchComponentDestroyCallbacks(*lightCmpt_);
            lightCmpt_->ClearOwner();
        }

        if (cameraCmpt_ != nullptr)
        {
            DispatchComponentDestroyCallbacks(*cameraCmpt_);
            cameraCmpt_->ClearOwner();
        }

        if (meshRenderCmpt_ != nullptr)
        {
            DispatchComponentDestroyCallbacks(*meshRenderCmpt_);
            meshRenderCmpt_->ClearOwner();
        }

        if (transformCmpt_ != nullptr)
        {
            DispatchComponentDestroyCallbacks(*transformCmpt_);
            transformCmpt_->ClearOwner();
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

    SizeT GameObject::GetComponentCount() const noexcept
    {
        SizeT count = 0;

        if (transformCmpt_ != nullptr)
        {
            ++count;
        }

        if (meshRenderCmpt_ != nullptr)
        {
            ++count;
        }

        if (cameraCmpt_ != nullptr)
        {
            ++count;
        }

        if (lightCmpt_ != nullptr)
        {
            ++count;
        }

        if (colliderCmpt_ != nullptr)
        {
            ++count;
        }

        if (rigidbodyCmpt_ != nullptr)
        {
            ++count;
        }

        return count + scriptableComponents_.size();
    }

    Component* GameObject::GetComponent(SizeT index) noexcept
    {
        SizeT visibleIndex = 0;
        Component* componentSlots[] = {
            transformCmpt_.get(),
            meshRenderCmpt_.get(),
            cameraCmpt_.get(),
            lightCmpt_.get(),
            colliderCmpt_.get(),
            rigidbodyCmpt_.get(),
        };
        for (Component* component : componentSlots)
        {
            if (component == nullptr)
            {
                continue;
            }

            if (visibleIndex == index)
            {
                return component;
            }

            ++visibleIndex;
        }

        const SizeT scriptIndex = index - visibleIndex;
        if (scriptIndex < scriptableComponents_.size())
        {
            return scriptableComponents_[scriptIndex].get();
        }

        return nullptr;
    }

    const Component* GameObject::GetComponent(SizeT index) const noexcept
    {
        SizeT visibleIndex = 0;
        const Component* componentSlots[] = {
            transformCmpt_.get(),
            meshRenderCmpt_.get(),
            cameraCmpt_.get(),
            lightCmpt_.get(),
            colliderCmpt_.get(),
            rigidbodyCmpt_.get(),
        };

        for (const Component* component : componentSlots)
        {
            if (component == nullptr)
            {
                continue;
            }

            if (visibleIndex == index)
            {
                return component;
            }

            ++visibleIndex;
        }

        const SizeT scriptIndex = index - visibleIndex;
        if (scriptIndex < scriptableComponents_.size())
        {
            return scriptableComponents_[scriptIndex].get();
        }

        return nullptr;
    }

    SizeT GameObject::GetScriptableComponentCount() const noexcept
    {
        return scriptableComponents_.size();
    }

    ScriptableComponent* GameObject::GetScriptableComponent(SizeT index) noexcept
    {
        return index < scriptableComponents_.size() ? scriptableComponents_[index].get() : nullptr;
    }

    const ScriptableComponent* GameObject::GetScriptableComponent(SizeT index) const noexcept
    {
        return index < scriptableComponents_.size() ? scriptableComponents_[index].get() : nullptr;
    }

    bool GameObject::RemoveScriptableComponent(const ScriptableComponent& component) noexcept
    {
        const auto componentIt = std::find_if(scriptableComponents_.begin(),
                                              scriptableComponents_.end(),
                                              [&component](const std::unique_ptr<ScriptableComponent>& candidate) { return candidate.get() == &component; });
        if (componentIt == scriptableComponents_.end() || *componentIt == nullptr)
        {
            return false;
        }

        DispatchComponentDestroyCallbacks(**componentIt);
        (*componentIt)->ClearOwner();
        scriptableComponents_.erase(componentIt);
        return true;
    }

    void GameObject::FixedUpdate(Float32 fixedDeltaSeconds)
    {
        Component* componentSlots[] = {
            transformCmpt_.get(),
            meshRenderCmpt_.get(),
            cameraCmpt_.get(),
            lightCmpt_.get(),
            colliderCmpt_.get(),
            rigidbodyCmpt_.get(),
        };
        for (Component* component : componentSlots)
        {
            if (component != nullptr && component->IsEnabled())
            {
                component->OnFixedUpdate(fixedDeltaSeconds);
            }
        }
        for (std::unique_ptr<ScriptableComponent>& script : scriptableComponents_)
        {
            if (script != nullptr && script->IsEnabled())
            {
                script->OnFixedUpdate(fixedDeltaSeconds);
            }
        }

        if (transformCmpt_ == nullptr)
        {
            return;
        }

        for (SizeT childIndex = 0; childIndex < transformCmpt_->GetChildCount(); ++childIndex)
        {
            GameObject* child = transformCmpt_->GetChildGameObject(childIndex);
            if (child != nullptr)
            {
                child->FixedUpdate(fixedDeltaSeconds);
            }
        }
    }

    void GameObject::Update(Float32 deltaSeconds)
    {
        Component* componentSlots[] = {
            transformCmpt_.get(),
            meshRenderCmpt_.get(),
            cameraCmpt_.get(),
            lightCmpt_.get(),
            colliderCmpt_.get(),
            rigidbodyCmpt_.get(),
        };
        for (Component* component : componentSlots)
        {
            if (component != nullptr && component->IsEnabled())
            {
                component->OnUpdate(deltaSeconds);
            }
        }
        for (std::unique_ptr<ScriptableComponent>& script : scriptableComponents_)
        {
            if (script != nullptr && script->IsEnabled())
            {
                script->OnUpdate(deltaSeconds);
            }
        }

        if (transformCmpt_ == nullptr)
        {
            return;
        }

        for (SizeT childIndex = 0; childIndex < transformCmpt_->GetChildCount(); ++childIndex)
        {
            GameObject* child = transformCmpt_->GetChildGameObject(childIndex);
            if (child != nullptr)
            {
                child->Update(deltaSeconds);
            }
        }
    }

    void GameObject::LateUpdate(Float32 deltaSeconds)
    {
        Component* componentSlots[] = {
            transformCmpt_.get(),
            meshRenderCmpt_.get(),
            cameraCmpt_.get(),
            lightCmpt_.get(),
            colliderCmpt_.get(),
            rigidbodyCmpt_.get(),
        };
        for (Component* component : componentSlots)
        {
            if (component != nullptr && component->IsEnabled())
            {
                component->OnLateUpdate(deltaSeconds);
            }
        }
        for (std::unique_ptr<ScriptableComponent>& script : scriptableComponents_)
        {
            if (script != nullptr && script->IsEnabled())
            {
                script->OnLateUpdate(deltaSeconds);
            }
        }

        if (transformCmpt_ == nullptr)
        {
            return;
        }

        for (SizeT childIndex = 0; childIndex < transformCmpt_->GetChildCount(); ++childIndex)
        {
            GameObject* child = transformCmpt_->GetChildGameObject(childIndex);
            if (child != nullptr)
            {
                child->LateUpdate(deltaSeconds);
            }
        }
    }

    void GameObject::InitializeRequiredComponents()
    {
        transformCmpt_ = std::make_unique<TransformComponent>(*scene_, *this);
    }

    bool GameObject::ShouldDispatchLifecycleCallbacks() const noexcept
    {
        return scene_ != nullptr && scene_->ShouldDispatchLifecycleCallbacks();
    }

    void GameObject::DispatchComponentCreateCallbacks(Component& component)
    {
        if (!ShouldDispatchLifecycleCallbacks())
        {
            return;
        }

        component.OnCreate();
        if (component.IsEnabled())
        {
            component.OnEnable();
        }
    }

    void GameObject::DispatchComponentDestroyCallbacks(Component& component) noexcept
    {
        if (!ShouldDispatchLifecycleCallbacks())
        {
            return;
        }

        if (component.IsEnabled())
        {
            component.SetEnabled(false);
        }
        component.OnDestroy();
    }

    void GameObject::DispatchLifecycleCreateCallbacksRecursive()
    {
        const SizeT childCount = transformCmpt_ != nullptr ? transformCmpt_->GetChildCount() : 0;
        Component* componentSlots[] = {
            transformCmpt_.get(),
            meshRenderCmpt_.get(),
            cameraCmpt_.get(),
            lightCmpt_.get(),
            colliderCmpt_.get(),
            rigidbodyCmpt_.get(),
        };
        for (Component* component : componentSlots)
        {
            if (component != nullptr)
            {
                component->OnCreate();
                if (component->IsEnabled())
                {
                    component->OnEnable();
                }
            }
        }
        for (std::unique_ptr<ScriptableComponent>& script : scriptableComponents_)
        {
            if (script != nullptr)
            {
                script->OnCreate();
                if (script->IsEnabled())
                {
                    script->OnEnable();
                }
            }
        }

        if (transformCmpt_ == nullptr)
        {
            return;
        }

        for (SizeT childIndex = 0; childIndex < childCount; ++childIndex)
        {
            GameObject* child = transformCmpt_->GetChildGameObject(childIndex);
            if (child != nullptr)
            {
                child->DispatchLifecycleCreateCallbacksRecursive();
            }
        }
    }
} // namespace ve
