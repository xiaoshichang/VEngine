#include "Engine/Runtime/Scripting/DotnetScriptableComponent.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/JsonUtils.h"
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

    ErrorCode DotnetScriptableComponent::EnsureScriptInstance(bool invokeOnCreate)
    {
        if (scriptTypeName_.empty())
        {
            return ErrorCode::InvalidArgument;
        }

        ErrorCode result = ErrorCode::None;
        if (scriptInstance_ == 0)
        {
            result = CreateScriptInstance(false);
            if (result != ErrorCode::None)
            {
                return result;
            }
        }

        if (invokeOnCreate && !lifecycleStarted_)
        {
            VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::EnsureScriptInstance requires a scripting system.");
            scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnCreate);
            lifecycleStarted_ = true;
        }

        return result;
    }

    Result<boost::json::object> DotnetScriptableComponent::GetScriptFields() const
    {
        if (scriptInstance_ == 0)
        {
            return Result<boost::json::object>::Failure(Error(ErrorCode::InvalidState, "Script fields require a managed script instance."));
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::GetScriptFields requires a scripting system.");
        Result<std::string> jsonText = scriptingSystem_->GetScriptFieldsJson(scriptInstance_);
        if (!jsonText)
        {
            return Result<boost::json::object>::Failure(jsonText.GetError());
        }

        Result<boost::json::value> json = JsonUtils::Parse(jsonText.GetValue());
        if (!json || !json.GetValue().is_object())
        {
            return Result<boost::json::object>::Failure(Error(ErrorCode::InvalidArgument, "Managed script fields JSON must be an object."));
        }

        return Result<boost::json::object>::Success(json.GetValue().as_object());
    }

    ErrorCode DotnetScriptableComponent::SetScriptFields(const boost::json::object& fields)
    {
        const ErrorCode ensureResult = EnsureScriptInstance(false);
        if (ensureResult != ErrorCode::None)
        {
            return ensureResult;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::SetScriptFields requires a scripting system.");
        return scriptingSystem_->SetScriptFieldsJson(scriptInstance_, boost::json::serialize(fields));
    }

    ErrorCode DotnetScriptableComponent::SetScriptField(std::string_view fieldName, const boost::json::value& value)
    {
        const ErrorCode ensureResult = EnsureScriptInstance(false);
        if (ensureResult != ErrorCode::None)
        {
            return ensureResult;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::SetScriptField requires a scripting system.");
        return scriptingSystem_->SetScriptFieldJson(scriptInstance_, fieldName, boost::json::serialize(value));
    }

    void DotnetScriptableComponent::OnCreate()
    {
        const ErrorCode result = EnsureScriptInstance(true);
        if (result != ErrorCode::None)
        {
            VE_LOG_ERROR_CATEGORY("Script", "Failed to start script '{}': {}", scriptTypeName_, ToString(result));
        }
    }

    void DotnetScriptableComponent::OnDestroy()
    {
        ReleaseScriptInstance();
    }

    void DotnetScriptableComponent::OnUpdate(Float32 deltaSeconds)
    {
        if (scriptInstance_ == 0 || !lifecycleStarted_)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::OnUpdate requires a scripting system.");
        scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnUpdate, deltaSeconds);
    }

    void DotnetScriptableComponent::OnLateUpdate(Float32 deltaSeconds)
    {
        if (scriptInstance_ == 0 || !lifecycleStarted_)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::OnLateUpdate requires a scripting system.");
        scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnLateUpdate, deltaSeconds);
    }

    void DotnetScriptableComponent::OnEnable()
    {
        if (scriptInstance_ == 0 || !lifecycleStarted_)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::OnEnable requires a scripting system.");
        scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnEnable);
    }

    void DotnetScriptableComponent::OnDisable()
    {
        if (scriptInstance_ == 0 || !lifecycleStarted_)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::OnDisable requires a scripting system.");
        scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnDisable);
    }

    ErrorCode DotnetScriptableComponent::CreateScriptInstance(bool invokeOnCreate)
    {
        if (scriptInstance_ != 0 || scriptTypeName_.empty())
        {
            return ErrorCode::None;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::CreateScriptInstance requires a scripting system.");
        Result<ScriptInstanceHandle> result = scriptingSystem_->CreateScriptInstance(ScriptInstanceDesc{this, scriptTypeName_, invokeOnCreate});
        if (!result)
        {
            VE_LOG_ERROR_CATEGORY("Script", "Failed to create script '{}': {}", scriptTypeName_, result.GetError().GetMessage());
            return result.GetError().GetCode();
        }

        scriptInstance_ = result.GetValue();
        lifecycleStarted_ = invokeOnCreate;
        return ErrorCode::None;
    }

    void DotnetScriptableComponent::ReleaseScriptInstance() noexcept
    {
        if (scriptInstance_ == 0)
        {
            return;
        }

        VE_ASSERT_MESSAGE(scriptingSystem_, "DotnetScriptableComponent::ReleaseScriptInstance requires a scripting system.");
        if (lifecycleStarted_ && IsEnabled())
        {
            scriptingSystem_->InvokeScriptEvent(scriptInstance_, ScriptLifecycleEvent::OnDisable);
        }
        scriptingSystem_->DestroyScriptInstance(scriptInstance_);
        scriptInstance_ = 0;
        lifecycleStarted_ = false;
    }
} // namespace ve
