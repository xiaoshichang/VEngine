#include "Engine/Runtime/Scripting/ScriptContext.h"

#include "Engine/Runtime/Logging/Log.h"

#include <vector>

namespace ve
{
    ScriptContext::ScriptContext(ScriptHost& host)
        : host_(&host)
    {
    }

    ScriptContext::~ScriptContext()
    {
        std::vector<ScriptInstanceId> instances(activeInstances_.begin(), activeInstances_.end());
        for (ScriptInstanceId instanceId : instances)
        {
            Result<ScriptOperationResult> destroyResult = host_->DestroyScriptInstance(instanceId);
            if (!destroyResult)
            {
                VE_LOG_WARN_CATEGORY("Script",
                                     "Failed to destroy managed script instance {} during ScriptContext teardown: {}",
                                     instanceId,
                                     destroyResult.GetError().GetMessage());
            }
        }

        activeInstances_.clear();
        if (projectAssemblyLoaded_)
        {
            Result<ScriptOperationResult> unloadResult = host_->UnloadProjectAssembly();
            if (!unloadResult)
            {
                VE_LOG_WARN_CATEGORY("Script",
                                     "Failed to unload project script assembly during ScriptContext teardown: {}",
                                     unloadResult.GetError().GetMessage());
            }

            projectAssemblyLoaded_ = false;
        }
    }

    Result<ScriptOperationResult> ScriptContext::LoadProjectAssembly(const Path& assemblyPath)
    {
        if (projectAssemblyLoaded_)
        {
            return Result<ScriptOperationResult>::Failure(
                Error(ErrorCode::InvalidState, "ScriptContext already has a loaded project script assembly."));
        }

        Result<ScriptOperationResult> result = host_->LoadProjectAssembly(assemblyPath);
        if (!result)
        {
            return result;
        }

        projectAssemblyPath_ = assemblyPath;
        projectAssemblyLoaded_ = true;
        return Result<ScriptOperationResult>::Success(ScriptOperationResult{});
    }

    bool ScriptContext::IsProjectAssemblyLoaded() const noexcept
    {
        return projectAssemblyLoaded_;
    }

    const Path& ScriptContext::GetProjectAssemblyPath() const noexcept
    {
        return projectAssemblyPath_;
    }

    void ScriptContext::SetRuntimeContext(InputSystem* inputSystem, Scene* scene) noexcept
    {
        host_->SetBridgeRuntimeContext(inputSystem, scene);
    }

    ScriptObjectHandle ScriptContext::RegisterScriptComponent(ScriptComponent& component)
    {
        return host_->GetBridgeRegistry().RegisterComponent(component);
    }

    void ScriptContext::UnregisterScriptComponent(ScriptObjectHandle handle, const ScriptComponent& component) noexcept
    {
        host_->GetBridgeRegistry().UnregisterComponent(handle, component);
    }

    Result<ScriptInstanceId> ScriptContext::CreateScriptInstance(std::string_view scriptTypeName,
                                                                 ScriptObjectHandle nativeHandle)
    {
        if (!projectAssemblyLoaded_)
        {
            return Result<ScriptInstanceId>::Failure(
                Error(ErrorCode::InvalidState, "ScriptContext requires a loaded project script assembly."));
        }

        Result<ScriptInstanceId> result = host_->CreateScriptInstance(scriptTypeName, nativeHandle);
        if (result)
        {
            activeInstances_.insert(result.GetValue());
        }

        return result;
    }

    Result<ScriptOperationResult> ScriptContext::InvokeLifecycle(ScriptInstanceId instanceId,
                                                                 ScriptLifecycleMethod lifecycle,
                                                                 float deltaSeconds)
    {
        if (activeInstances_.find(instanceId) == activeInstances_.end())
        {
            return Result<ScriptOperationResult>::Failure(
                Error(ErrorCode::InvalidArgument, "ScriptContext does not own the requested script instance."));
        }

        return host_->InvokeLifecycle(instanceId, lifecycle, deltaSeconds);
    }

    Result<ScriptOperationResult> ScriptContext::DestroyScriptInstance(ScriptInstanceId instanceId)
    {
        const auto iter = activeInstances_.find(instanceId);
        if (iter == activeInstances_.end())
        {
            return Result<ScriptOperationResult>::Failure(
                Error(ErrorCode::InvalidArgument, "ScriptContext does not own the requested script instance."));
        }

        Result<ScriptOperationResult> result = host_->DestroyScriptInstance(instanceId);
        activeInstances_.erase(iter);
        return result;
    }
} // namespace ve
