#include "Engine/Runtime/Scene/GameObject.h"

namespace ve
{
    GameObject::GameObject()
    {
        InitializeRequiredComponents();
    }

    GameObject::GameObject(std::string name)
        : name_(std::move(name))
    {
        InitializeRequiredComponents();
    }

    GameObject::~GameObject()
    {
        if (lightCmpt_ != nullptr)
        {
            lightCmpt_->SetOwner(nullptr);
        }

        if (cameraCmpt_ != nullptr)
        {
            cameraCmpt_->SetOwner(nullptr);
        }

        if (meshRenderCmpt_ != nullptr)
        {
            meshRenderCmpt_->SetOwner(nullptr);
        }

        if (transformCmpt_ != nullptr)
        {
            transformCmpt_->SetOwner(nullptr);
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

    void GameObject::InitializeRequiredComponents()
    {
        transformCmpt_ = std::make_unique<TransformComponent>();
        transformCmpt_->SetOwner(this);
    }
} // namespace ve
