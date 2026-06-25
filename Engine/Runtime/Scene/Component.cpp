#include "Engine/Runtime/Scene/Component.h"

#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneSystem.h"

namespace ve
{
    Component::Component(Scene& scene, GameObject& owner) noexcept
        : owner_(&owner)
        , scene_(&scene)
    {
    }

    GameObject* Component::GetOwner() noexcept
    {
        return owner_;
    }

    const GameObject* Component::GetOwner() const noexcept
    {
        return owner_;
    }

    bool Component::IsEnabled() const noexcept
    {
        return enabled_;
    }

    void Component::SetEnabled(bool enabled) noexcept
    {
        if (enabled_ == enabled)
        {
            return;
        }

        enabled_ = enabled;
        const SceneSystem* sceneSystem = scene_ != nullptr ? scene_->GetSceneSystem() : nullptr;
        if (sceneSystem == nullptr || !sceneSystem->ShouldDispatchLifecycleCallbacks())
        {
            return;
        }

        if (enabled_)
        {
            OnEnable();
        }
        else
        {
            OnDisable();
        }
    }

    void Component::OnCreate() {}

    void Component::OnDestroy() {}

    void Component::OnUpdate(Float32 deltaSeconds)
    {
        static_cast<void>(deltaSeconds);
    }

    void Component::OnLateUpdate(Float32 deltaSeconds)
    {
        static_cast<void>(deltaSeconds);
    }

    void Component::OnEnable() {}

    void Component::OnDisable() {}

    void Component::ClearOwner() noexcept
    {
        owner_ = nullptr;
    }

    Scene* Component::GetScene() noexcept
    {
        return scene_;
    }

    const Scene* Component::GetScene() const noexcept
    {
        return scene_;
    }

} // namespace ve
