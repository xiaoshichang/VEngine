#include "Engine/Runtime/Scripting/ScriptComponent.h"

#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Reflection/ReflectionRegistry.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scripting/ScriptContext.h"
#include "Engine/Runtime/Time/Time.h"

#include <boost/json.hpp>

#include <memory>
#include <utility>

namespace ve
{
    namespace
    {
        [[nodiscard]] std::string MakeComponentLabel(const ScriptComponent& component)
        {
            std::string label = component.GetGameObject().GetName();
            if (label.empty())
            {
                label = "<unnamed>";
            }

            if (!component.GetScriptTypeName().empty())
            {
                label += " / ";
                label += component.GetScriptTypeName();
            }

            return label;
        }

        [[nodiscard]] boost::json::value ToJsonString(const std::string& value)
        {
            return boost::json::value(value);
        }

        [[nodiscard]] std::string ToString(const boost::json::value& jsonValue, const std::string& fallback)
        {
            return jsonValue.is_string() ? std::string(jsonValue.as_string().c_str()) : fallback;
        }
    } // namespace

    ScriptComponent::~ScriptComponent()
    {
        DestroyManagedInstance();
    }

    const std::string& ScriptComponent::GetScriptTypeName() const noexcept
    {
        return scriptTypeName_;
    }

    void ScriptComponent::SetScriptTypeName(std::string scriptTypeName)
    {
        GetScene().ValidateMutationAccess();
        if (scriptTypeName_ == scriptTypeName)
        {
            return;
        }

        DestroyManagedInstance();
        scriptTypeName_ = std::move(scriptTypeName);
        lastError_.clear();
        failed_ = false;

        if (nativeLifecycleCreated_ && !scriptTypeName_.empty() && EnsureManagedInstance(false) &&
            IsActiveAndEnabled())
        {
            (void)InvokeManagedLifecycle(ScriptLifecycleMethod::OnEnable);
        }
    }

    const std::string& ScriptComponent::GetAssemblyName() const noexcept
    {
        return assemblyName_;
    }

    void ScriptComponent::SetAssemblyName(std::string assemblyName)
    {
        GetScene().ValidateMutationAccess();
        assemblyName_ = std::move(assemblyName);
    }

    bool ScriptComponent::IsScriptValid() const noexcept
    {
        return scriptValid_;
    }

    const std::string& ScriptComponent::GetLastError() const noexcept
    {
        return lastError_;
    }

    void ScriptComponent::OnCreate()
    {
        nativeLifecycleCreated_ = true;
        if (!scriptTypeName_.empty())
        {
            (void)EnsureManagedInstance(false);
        }
    }

    void ScriptComponent::OnDestroy()
    {
        if (instanceId_ != InvalidScriptInstanceId)
        {
            (void)InvokeManagedLifecycle(ScriptLifecycleMethod::OnDestroy);
        }

        DestroyManagedInstance();
        nativeLifecycleCreated_ = false;
    }

    void ScriptComponent::OnEnable()
    {
        if (EnsureManagedInstance(false) && scriptValid_)
        {
            (void)InvokeManagedLifecycle(ScriptLifecycleMethod::OnEnable);
        }
    }

    void ScriptComponent::OnDisable()
    {
        if (instanceId_ != InvalidScriptInstanceId && scriptValid_)
        {
            (void)InvokeManagedLifecycle(ScriptLifecycleMethod::OnDisable);
        }
    }

    void ScriptComponent::OnUpdate()
    {
        if (scriptTypeName_.empty())
        {
            if (!failed_)
            {
                ReportFailure("ScriptComponent requires scriptTypeName before it can run.");
            }
            return;
        }

        if (EnsureManagedInstance(true) && scriptValid_)
        {
            (void)InvokeManagedLifecycle(ScriptLifecycleMethod::OnUpdate, Time::GetDeltaSeconds());
        }
    }

    ScriptContext* ScriptComponent::GetActiveScriptContext() noexcept
    {
        return GetScene().GetScriptContext();
    }

    bool ScriptComponent::EnsureManagedInstance(bool reportMissingContext)
    {
        if (instanceId_ != InvalidScriptInstanceId)
        {
            return true;
        }

        if (failed_)
        {
            return false;
        }

        if (scriptTypeName_.empty())
        {
            return false;
        }

        ScriptContext* context = GetActiveScriptContext();
        if (context == nullptr)
        {
            if (reportMissingContext)
            {
                ReportFailure("ScriptComponent cannot create '" + scriptTypeName_ + "' because the Scene has no "
                              "ScriptContext.");
            }
            return false;
        }

        if (nativeHandle_ == InvalidScriptObjectHandle)
        {
            nativeHandle_ = context->RegisterScriptComponent(*this);
        }

        Result<ScriptInstanceId> createResult = context->CreateScriptInstance(scriptTypeName_, nativeHandle_);
        if (!createResult)
        {
            ReportFailure(createResult.GetError().GetMessage());
            context->UnregisterScriptComponent(nativeHandle_, *this);
            nativeHandle_ = InvalidScriptObjectHandle;
            return false;
        }

        instanceId_ = createResult.GetValue();
        scriptValid_ = true;

        if (!InvokeManagedLifecycle(ScriptLifecycleMethod::OnCreate))
        {
            return false;
        }

        return true;
    }

    bool ScriptComponent::InvokeManagedLifecycle(ScriptLifecycleMethod lifecycle, float deltaSeconds)
    {
        if (instanceId_ == InvalidScriptInstanceId)
        {
            return false;
        }

        ScriptContext* context = GetActiveScriptContext();
        if (context == nullptr)
        {
            ReportFailure("ScriptComponent cannot invoke lifecycle because the Scene has no ScriptContext.");
            return false;
        }

        Result<ScriptOperationResult> invokeResult = context->InvokeLifecycle(instanceId_, lifecycle, deltaSeconds);
        if (!invokeResult)
        {
            ReportFailure(invokeResult.GetError().GetMessage());
            return false;
        }

        return true;
    }

    void ScriptComponent::DestroyManagedInstance() noexcept
    {
        ScriptContext* context = nullptr;
        if (nativeLifecycleCreated_)
        {
            context = GetActiveScriptContext();
        }

        if (context != nullptr && instanceId_ != InvalidScriptInstanceId)
        {
            Result<ScriptOperationResult> destroyResult = context->DestroyScriptInstance(instanceId_);
            if (!destroyResult)
            {
                VE_LOG_ERROR_CATEGORY("Script",
                                      "Failed to destroy managed script instance on '{}': {}",
                                      MakeComponentLabel(*this),
                                      destroyResult.GetError().GetMessage());
            }
        }

        if (context != nullptr && nativeHandle_ != InvalidScriptObjectHandle)
        {
            context->UnregisterScriptComponent(nativeHandle_, *this);
        }

        ResetRuntimeState();
    }

    void ScriptComponent::ReportFailure(std::string message)
    {
        if (message.empty())
        {
            message = "Unknown script failure.";
        }

        lastError_ = std::move(message);
        scriptValid_ = false;
        failed_ = true;
        VE_LOG_ERROR_CATEGORY("Script", "ScriptComponent '{}' failed: {}", MakeComponentLabel(*this), lastError_);
    }

    void ScriptComponent::ResetRuntimeState() noexcept
    {
        nativeHandle_ = InvalidScriptObjectHandle;
        instanceId_ = InvalidScriptInstanceId;
        scriptValid_ = false;
    }

    void RegisterScriptReflectionTypes(ReflectionRegistry& registry)
    {
        ReflectedTypeInfo script;
        script.name = "ScriptComponent";
        script.baseTypeName = "Component";
        script.componentFactory = []() { return std::make_unique<ScriptComponent>(); };
        script.properties.push_back(ReflectedPropertyInfo{
            "scriptTypeName",
            ReflectedPropertyType::String,
            true,
            true,
            {},
            [](const Component& component)
            { return ToJsonString(static_cast<const ScriptComponent&>(component).GetScriptTypeName()); },
            [](Component& component, const boost::json::value& jsonValue)
            {
                auto& scriptComponent = static_cast<ScriptComponent&>(component);
                scriptComponent.SetScriptTypeName(ToString(jsonValue, scriptComponent.GetScriptTypeName()));
            }});
        script.properties.push_back(ReflectedPropertyInfo{
            "assemblyName",
            ReflectedPropertyType::String,
            true,
            true,
            {},
            [](const Component& component)
            { return ToJsonString(static_cast<const ScriptComponent&>(component).GetAssemblyName()); },
            [](Component& component, const boost::json::value& jsonValue)
            {
                auto& scriptComponent = static_cast<ScriptComponent&>(component);
                scriptComponent.SetAssemblyName(ToString(jsonValue, scriptComponent.GetAssemblyName()));
            }});
        registry.RegisterType(std::move(script));
    }
} // namespace ve
