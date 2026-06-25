#pragma once

#include "Engine/Runtime/Scripting/ScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptingTypes.h"

#include <string>

namespace ve
{
    class ScriptingSystem;

    class DotnetScriptableComponent final : public ScriptableComponent
    {
    public:
        DotnetScriptableComponent(Scene& scene, GameObject& owner, std::string scriptTypeName, ScriptingSystem& scriptingSystem);
        ~DotnetScriptableComponent() override;

        [[nodiscard]] ScriptingSystem* GetScriptingSystem() noexcept override;
        [[nodiscard]] const ScriptingSystem* GetScriptingSystem() const noexcept override;

        [[nodiscard]] const std::string& GetScriptTypeName() const noexcept override;
        [[nodiscard]] bool HasScriptInstance() const noexcept override;

        void OnCreate() override;
        void OnDestroy() override;
        void OnUpdate(Float32 deltaSeconds) override;
        void OnLateUpdate(Float32 deltaSeconds) override;
        void OnEnable() override;
        void OnDisable() override;

    private:
        void CreateScriptInstance();
        void ReleaseScriptInstance() noexcept;

        ScriptingSystem* scriptingSystem_ = nullptr;
        std::string scriptTypeName_;
        ScriptInstanceHandle scriptInstance_ = 0;
    };
} // namespace ve
