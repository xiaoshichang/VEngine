#include "Engine/Runtime/Scripting/ScriptableComponent.h"

#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"

#include <utility>

namespace ve
{
    ScriptableComponent::ScriptableComponent(Scene& scene, GameObject& owner)
        : Component(scene, owner)
    {
    }

    ScriptableComponent::ScriptableComponent(Scene& scene, GameObject& owner, std::string scriptTypeName, ScriptingSystem* scriptingSystem)
        : Component(scene, owner)
        , scriptingSystem_(scriptingSystem)
        , scriptTypeName_(std::move(scriptTypeName))
    {
    }

    ScriptableComponent::~ScriptableComponent()
    {
        ReleaseScriptInstance();
    }

    void ScriptableComponent::SetScriptingSystem(ScriptingSystem* scriptingSystem) noexcept
    {
        if (scriptingSystem_ == scriptingSystem)
        {
            return;
        }

        ReleaseScriptInstance();
        scriptingSystem_ = scriptingSystem;
        createAttempted_ = false;
        EnsureScriptInstance();
    }

    ScriptingSystem* ScriptableComponent::GetScriptingSystem() noexcept
    {
        return scriptingSystem_;
    }

    const ScriptingSystem* ScriptableComponent::GetScriptingSystem() const noexcept
    {
        return scriptingSystem_;
    }

    void ScriptableComponent::SetScriptTypeName(std::string scriptTypeName)
    {
        if (scriptTypeName_ == scriptTypeName)
        {
            return;
        }

        ReleaseScriptInstance();
        scriptTypeName_ = std::move(scriptTypeName);
        createAttempted_ = false;
        EnsureScriptInstance();
    }

    const std::string& ScriptableComponent::GetScriptTypeName() const noexcept
    {
        return scriptTypeName_;
    }

    bool ScriptableComponent::HasScriptInstance() const noexcept
    {
        return scriptInstance_ != 0;
    }

    void ScriptableComponent::OnCreate()
    {
        EnsureScriptInstance();
    }

    void ScriptableComponent::OnDestroy()
    {
        ReleaseScriptInstance();
    }

    void ScriptableComponent::OnUpdate(Float32 deltaSeconds)
    {
        EnsureScriptInstance();
        if (scriptingSystem_ != nullptr && scriptInstance_ != 0)
        {
            scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnUpdate, deltaSeconds);
        }
    }

    void ScriptableComponent::OnLateUpdate(Float32 deltaSeconds)
    {
        EnsureScriptInstance();
        if (scriptingSystem_ != nullptr && scriptInstance_ != 0)
        {
            scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnLateUpdate, deltaSeconds);
        }
    }

    void ScriptableComponent::OnEnable()
    {
        EnsureScriptInstance();
        if (scriptingSystem_ != nullptr && scriptInstance_ != 0)
        {
            scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnEnable);
        }
    }

    void ScriptableComponent::OnDisable()
    {
        if (scriptingSystem_ != nullptr && scriptInstance_ != 0)
        {
            scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnDisable);
        }
    }

    void ScriptableComponent::EnsureScriptInstance()
    {
        if (scriptInstance_ != 0 || createAttempted_ || scriptingSystem_ == nullptr || scriptTypeName_.empty())
        {
            return;
        }

        createAttempted_ = true;
        Result<ScriptInstanceHandle> result = scriptingSystem_->CreateScriptInstance(ScriptInstanceDesc{this, scriptTypeName_});
        if (!result)
        {
            VE_LOG_ERROR_CATEGORY("Script", "Failed to create script '{}': {}", scriptTypeName_, result.GetError().GetMessage());
            return;
        }

        scriptInstance_ = result.GetValue();
    }

    void ScriptableComponent::ReleaseScriptInstance() noexcept
    {
        if (scriptingSystem_ == nullptr || scriptInstance_ == 0)
        {
            scriptInstance_ = 0;
            return;
        }

        if (IsEnabled())
        {
            scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnDisable);
        }
        scriptingSystem_->DestroyScriptInstance(scriptInstance_);
        scriptInstance_ = 0;
    }
} // namespace ve
