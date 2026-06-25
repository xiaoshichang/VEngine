#include "Engine/Runtime/Scripting/ScriptableComponent.h"

namespace ve
{
    ScriptableComponent::ScriptableComponent(Scene& scene, GameObject& owner) noexcept
        : Component(scene, owner)
    {
    }
} // namespace ve
