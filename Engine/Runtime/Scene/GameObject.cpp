#include "Engine/Runtime/Scene/GameObject.h"

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
        if (scriptableCmpt_ != nullptr)
        {
            if (scriptableCmpt_->IsEnabled())
            {
                scriptableCmpt_->SetEnabled(false);
            }
            scriptableCmpt_->OnDestroy();
            scriptableCmpt_->ClearOwner();
        }

        if (lightCmpt_ != nullptr)
        {
            if (lightCmpt_->IsEnabled())
            {
                lightCmpt_->SetEnabled(false);
            }
            lightCmpt_->OnDestroy();
            lightCmpt_->ClearOwner();
        }

        if (cameraCmpt_ != nullptr)
        {
            if (cameraCmpt_->IsEnabled())
            {
                cameraCmpt_->SetEnabled(false);
            }
            cameraCmpt_->OnDestroy();
            cameraCmpt_->ClearOwner();
        }

        if (meshRenderCmpt_ != nullptr)
        {
            if (meshRenderCmpt_->IsEnabled())
            {
                meshRenderCmpt_->SetEnabled(false);
            }
            meshRenderCmpt_->OnDestroy();
            meshRenderCmpt_->ClearOwner();
        }

        if (transformCmpt_ != nullptr)
        {
            if (transformCmpt_->IsEnabled())
            {
                transformCmpt_->SetEnabled(false);
            }
            transformCmpt_->OnDestroy();
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

        if (scriptableCmpt_ != nullptr)
        {
            ++count;
        }

        return count;
    }

    Component* GameObject::GetComponent(SizeT index) noexcept
    {
        SizeT visibleIndex = 0;
        Component* componentSlots[] = {
            transformCmpt_.get(),
            meshRenderCmpt_.get(),
            cameraCmpt_.get(),
            lightCmpt_.get(),
            scriptableCmpt_.get(),
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
            scriptableCmpt_.get(),
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

        return nullptr;
    }

    void GameObject::Update(Float32 deltaSeconds)
    {
        Component* componentSlots[] = {
            transformCmpt_.get(),
            meshRenderCmpt_.get(),
            cameraCmpt_.get(),
            lightCmpt_.get(),
            scriptableCmpt_.get(),
        };
        for (Component* component : componentSlots)
        {
            if (component != nullptr && component->IsEnabled())
            {
                component->OnUpdate(deltaSeconds);
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
            scriptableCmpt_.get(),
        };
        for (Component* component : componentSlots)
        {
            if (component != nullptr && component->IsEnabled())
            {
                component->OnLateUpdate(deltaSeconds);
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
        transformCmpt_->OnCreate();
        if (transformCmpt_->IsEnabled())
        {
            transformCmpt_->OnEnable();
        }
    }
} // namespace ve
