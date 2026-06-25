#include "Engine/Runtime/Scripting/DotnetScriptableComponent.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scripting/ScriptingSystem.h"

#include <utility>

namespace ve
{
    DotnetScriptableComponent::DotnetScriptableComponent(Scene& scene, GameObject& owner, std::string scriptTypeName, ScriptingSystem& scriptingSystem)
        : ScriptableComponent(scene, owner)
        , scriptingSystem_(&scriptingSystem)
        , scriptTypeName_(std::move(scriptTypeName))
    {
        CreateScriptInstance();
    }

    DotnetScriptableComponent::~DotnetScriptableComponent()
    {
        ReleaseScriptInstance();
    }

    ScriptingSystem* DotnetScriptableComponent::GetScriptingSystem() noexcept
    {
        return scriptingSystem_;
    }

    const ScriptingSystem* DotnetScriptableComponent::GetScriptingSystem() const noexcept
    {
        return scriptingSystem_;
    }

    const std::string& DotnetScriptableComponent::GetScriptTypeName() const noexcept
    {
        return scriptTypeName_;
    }

    bool DotnetScriptableComponent::HasScriptInstance() const noexcept
    {
        return scriptInstance_ != 0;
    }

    void DotnetScriptableComponent::OnCreate()
    {
    }

    void DotnetScriptableComponent::OnDestroy()
    {
    }

    void DotnetScriptableComponent::OnUpdate(Float32 deltaSeconds)
    {
        if (scriptInstance_ == 0)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::OnUpdate requires a scripting system.");
        scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnUpdate, deltaSeconds);
    }

    void DotnetScriptableComponent::OnLateUpdate(Float32 deltaSeconds)
    {
        if (scriptInstance_ == 0)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::OnLateUpdate requires a scripting system.");
        scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnLateUpdate, deltaSeconds);
    }

    void DotnetScriptableComponent::OnEnable()
    {
        if (scriptInstance_ == 0)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::OnEnable requires a scripting system.");
        scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnEnable);
    }

    void DotnetScriptableComponent::OnDisable()
    {
        if (scriptInstance_ == 0)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::OnDisable requires a scripting system.");
        scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnDisable);
    }

    void DotnetScriptableComponent::CreateScriptInstance()
    {
        if (scriptInstance_ != 0 || scriptTypeName_.empty())
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::CreateScriptInstance requires a scripting system.");
        Result<ScriptInstanceHandle> result = scriptingSystem_->CreateScriptInstance(ScriptInstanceDesc{this, scriptTypeName_});
        if (!result)
        {
            VE_LOG_ERROR_CATEGORY("Script", "Failed to create script '{}': {}", scriptTypeName_, result.GetError().GetMessage());
            return;
        }

        scriptInstance_ = result.GetValue();
    }

    void DotnetScriptableComponent::ReleaseScriptInstance() noexcept
    {
        if (scriptInstance_ == 0)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::ReleaseScriptInstance requires a scripting system.");
        if (IsEnabled())
        {
            scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnDisable);
        }
        scriptingSystem_->DestroyScriptInstance(scriptInstance_);
        scriptInstance_ = 0;
    }
} // namespace ve
