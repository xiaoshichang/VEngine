#include "Engine/Runtime/Scene/Component.h"

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
        enabled_ = enabled;
    }

    void Component::OnUpdate(Float32 deltaSeconds)
    {
        static_cast<void>(deltaSeconds);
    }

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
