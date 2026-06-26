#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"
#include "Engine/Runtime/Scripting/ScriptingTypes.h"

#include <boost/json.hpp>
#include <string>
#include <string_view>

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
        [[nodiscard]] ErrorCode EnsureScriptInstance(bool invokeOnCreate);
        [[nodiscard]] Result<boost::json::object> GetScriptFields() const;
        [[nodiscard]] ErrorCode SetScriptFields(const boost::json::object& fields);
        [[nodiscard]] ErrorCode SetScriptField(std::string_view fieldName, const boost::json::value& value);

        void OnCreate() override;
        void OnDestroy() override;
        void OnUpdate(Float32 deltaSeconds) override;
        void OnLateUpdate(Float32 deltaSeconds) override;
        void OnEnable() override;
        void OnDisable() override;

    private:
        [[nodiscard]] ErrorCode CreateScriptInstance(bool invokeOnCreate);
        void ReleaseScriptInstance() noexcept;

        ScriptingSystem* scriptingSystem_ = nullptr;
        std::string scriptTypeName_;
        ScriptInstanceHandle scriptInstance_ = 0;
        bool lifecycleStarted_ = false;
    };
} // namespace ve
