#include "Engine/Runtime/Scene/Component.h"

namespace ve
{
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

    void Component::SetOwner(GameObject* owner) noexcept
    {
        owner_ = owner;
    }
} // namespace ve
