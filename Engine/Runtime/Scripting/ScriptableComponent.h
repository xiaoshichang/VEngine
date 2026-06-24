#pragma once

#include "Engine/Runtime/Scene/Component.h"
#include "Engine/Runtime/Scripting/ScriptingTypes.h"

#include <string>

namespace ve
{
    class ScriptingSystem;

    class ScriptableComponent final : public Component
    {
    public:
        ScriptableComponent(Scene& scene, GameObject& owner);
        ScriptableComponent(Scene& scene, GameObject& owner, std::string scriptTypeName, ScriptingSystem* scriptingSystem = nullptr);
        ~ScriptableComponent() override;

        void SetScriptingSystem(ScriptingSystem* scriptingSystem) noexcept;
        [[nodiscard]] ScriptingSystem* GetScriptingSystem() noexcept;
        [[nodiscard]] const ScriptingSystem* GetScriptingSystem() const noexcept;

        void SetScriptTypeName(std::string scriptTypeName);
        [[nodiscard]] const std::string& GetScriptTypeName() const noexcept;
        [[nodiscard]] bool HasScriptInstance() const noexcept;

        void OnCreate() override;
        void OnDestroy() override;
        void OnUpdate(Float32 deltaSeconds) override;
        void OnLateUpdate(Float32 deltaSeconds) override;
        void OnEnable() override;
        void OnDisable() override;

    private:
        void EnsureScriptInstance();
        void ReleaseScriptInstance() noexcept;

        ScriptingSystem* scriptingSystem_ = nullptr;
        std::string scriptTypeName_;
        ScriptInstanceHandle scriptInstance_ = 0;
        bool createAttempted_ = false;
    };
} // namespace ve
