#pragma once

#include "Engine/Runtime/Scene/Component.h"

#include <string>

namespace ve
{
    class ScriptingSystem;

    class ScriptableComponent : public Component
    {
    public:
        ~ScriptableComponent() override = default;

        [[nodiscard]] virtual ScriptingSystem* GetScriptingSystem() noexcept = 0;
        [[nodiscard]] virtual const ScriptingSystem* GetScriptingSystem() const noexcept = 0;

        [[nodiscard]] virtual const std::string& GetScriptTypeName() const noexcept = 0;
        [[nodiscard]] virtual bool HasScriptInstance() const noexcept = 0;

    protected:
        ScriptableComponent(Scene& scene, GameObject& owner) noexcept;
    };
} // namespace ve
