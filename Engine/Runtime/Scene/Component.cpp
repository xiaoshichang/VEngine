#include "Engine/Runtime/Scene/Component.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"

namespace ve
{
    Component::~Component() = default;

    GameObject& Component::GetGameObject() noexcept
    {
        VE_ASSERT_MESSAGE(owner_ != nullptr, "Component is not attached to a GameObject.");
        return *owner_;
    }

    const GameObject& Component::GetGameObject() const noexcept
    {
        VE_ASSERT_MESSAGE(owner_ != nullptr, "Component is not attached to a GameObject.");
        return *owner_;
    }

    Scene& Component::GetScene() noexcept
    {
        return GetGameObject().GetScene();
    }

    const Scene& Component::GetScene() const noexcept
    {
        return GetGameObject().GetScene();
    }

    bool Component::IsEnabled() const noexcept
    {
        return enabled_;
    }

    void Component::SetEnabled(bool enabled)
    {
        GetScene().ValidateMutationAccess();
        if (enabled_ == enabled)
        {
            return;
        }

        enabled_ = enabled;
        GetGameObject().RefreshComponentsActiveState();
    }

    bool Component::IsActiveAndEnabled() const noexcept
    {
        return activeAndEnabled_;
    }

    void Component::AttachToGameObject(GameObject& owner) noexcept
    {
        VE_ASSERT_MESSAGE(owner_ == nullptr, "Component is already attached to a GameObject.");
        owner_ = &owner;
    }

    void Component::DetachFromGameObject() noexcept
    {
        owner_ = nullptr;
    }

    void Component::DispatchCreate()
    {
        VE_ASSERT_MESSAGE(!created_, "Component OnCreate should run only once.");
        created_ = true;
        OnCreate();
    }

    void Component::DispatchDestroy()
    {
        if (!created_)
        {
            return;
        }

        OnDestroy();
        created_ = false;
    }

    void Component::DispatchEnable()
    {
        if (activeAndEnabled_)
        {
            return;
        }

        activeAndEnabled_ = true;
        OnEnable();
    }

    void Component::DispatchDisable()
    {
        if (!activeAndEnabled_)
        {
            return;
        }

        activeAndEnabled_ = false;
        OnDisable();
    }

    void Component::DispatchUpdate()
    {
        if (activeAndEnabled_)
        {
            OnUpdate();
        }
    }

    void Component::DispatchFixedUpdate(Float32 fixedDeltaSeconds)
    {
        if (activeAndEnabled_)
        {
            OnFixedUpdate(fixedDeltaSeconds);
        }
    }

    void Component::DispatchLateUpdate()
    {
        if (activeAndEnabled_)
        {
            OnLateUpdate();
        }
    }
} // namespace ve
