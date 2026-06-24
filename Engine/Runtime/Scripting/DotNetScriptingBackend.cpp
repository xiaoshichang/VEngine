#include "Engine/Runtime/Scripting/DotNetScriptingBackend.h"

#include "Engine/Runtime/Logging/Log.h"

#include <utility>

namespace ve
{
    DotNetScriptingBackend::~DotNetScriptingBackend()
    {
        Shutdown();
    }

    ErrorCode DotNetScriptingBackend::Initialize(const ScriptingSystemInitParam& initParam)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        const ErrorCode result = InitializeHost(initParam);
        if (result != ErrorCode::None)
        {
            return result;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void DotNetScriptingBackend::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

        entryPoints_ = {};
        assemblyLoaded_ = false;
        initialized_ = false;
        ShutdownHost();
    }

    ErrorCode DotNetScriptingBackend::LoadAssembly(const ScriptingAssemblyLoadDesc& desc)
    {
        if (!initialized_)
        {
            return ErrorCode::InvalidState;
        }

        if (desc.assemblyPath.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        ManagedScriptEntryPoints entryPoints;
        const ErrorCode result = LoadManagedEntryPoints(desc, entryPoints);
        if (result != ErrorCode::None)
        {
            return result;
        }

        if (entryPoints.create == nullptr || entryPoints.destroy == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        entryPoints_ = entryPoints;
        assemblyLoaded_ = true;
        VE_LOG_INFO_CATEGORY("Script", "Loaded managed script assembly {}.", desc.assemblyPath.GetString());
        return ErrorCode::None;
    }

    Result<ScriptInstanceHandle> DotNetScriptingBackend::CreateScriptInstance(const ScriptInstanceDesc& desc)
    {
        if (!initialized_ || !assemblyLoaded_)
        {
            return Result<ScriptInstanceHandle>::Failure(Error(ErrorCode::InvalidState, "Managed script assembly is not loaded."));
        }

        if (desc.component == nullptr || desc.typeName.empty())
        {
            return Result<ScriptInstanceHandle>::Failure(Error(ErrorCode::InvalidArgument, "Script instance creation requires a component and script type name."));
        }

        ScriptInstanceHandle handle = entryPoints_.create(desc.component, desc.typeName.c_str());
        if (handle == 0)
        {
            return Result<ScriptInstanceHandle>::Failure(Error(ErrorCode::InvalidState, "Managed script instance creation returned an empty handle."));
        }

        return Result<ScriptInstanceHandle>::Success(handle);
    }

    void DotNetScriptingBackend::DestroyScriptInstance(ScriptInstanceHandle script) noexcept
    {
        if (script == 0 || entryPoints_.destroy == nullptr)
        {
            return;
        }

        entryPoints_.destroy(script);
    }

    void DotNetScriptingBackend::InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds) noexcept
    {
        if (script == 0)
        {
            return;
        }

        switch (event)
        {
        case ScriptLifecycleEvent::OnCreate:
            break;
        case ScriptLifecycleEvent::OnDestroy:
            DestroyScriptInstance(script);
            break;
        case ScriptLifecycleEvent::OnUpdate:
            if (entryPoints_.update != nullptr)
            {
                entryPoints_.update(script, deltaSeconds);
            }
            break;
        case ScriptLifecycleEvent::OnLateUpdate:
            if (entryPoints_.lateUpdate != nullptr)
            {
                entryPoints_.lateUpdate(script, deltaSeconds);
            }
            break;
        case ScriptLifecycleEvent::OnEnable:
            if (entryPoints_.enable != nullptr)
            {
                entryPoints_.enable(script);
            }
            break;
        case ScriptLifecycleEvent::OnDisable:
            if (entryPoints_.disable != nullptr)
            {
                entryPoints_.disable(script);
            }
            break;
        }
    }

    const std::filesystem::path& DotNetScriptingBackend::GetRuntimeRoot() const noexcept
    {
        return runtimeRoot_;
    }

    const std::filesystem::path& DotNetScriptingBackend::GetRuntimeConfigPath() const noexcept
    {
        return runtimeConfigPath_;
    }

    void DotNetScriptingBackend::SetResolvedRuntimePaths(std::filesystem::path runtimeRoot, std::filesystem::path runtimeConfigPath)
    {
        runtimeRoot_ = std::move(runtimeRoot);
        runtimeConfigPath_ = std::move(runtimeConfigPath);
    }
} // namespace ve
