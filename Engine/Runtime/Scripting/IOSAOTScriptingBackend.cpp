#include "Engine/Runtime/Scripting/IOSAOTScriptingBackend.h"

namespace ve
{
    ErrorCode IOSAOTScriptingBackend::Initialize(const ScriptingSystemInitParam& initParam)
    {
        static_cast<void>(initParam);
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void IOSAOTScriptingBackend::Shutdown() noexcept
    {
        initialized_ = false;
    }

    ScriptingBackendType IOSAOTScriptingBackend::GetBackendType() const noexcept
    {
        return ScriptingBackendType::IOSAOT;
    }

    ErrorCode IOSAOTScriptingBackend::LoadAssembly(const ScriptingAssemblyLoadDesc& desc)
    {
        static_cast<void>(desc);
        return initialized_ ? ErrorCode::Unsupported : ErrorCode::InvalidState;
    }

    ErrorCode IOSAOTScriptingBackend::LoadProjectAssembly(const ScriptingProjectAssemblyLoadDesc& desc)
    {
        static_cast<void>(desc);
        return initialized_ ? ErrorCode::Unsupported : ErrorCode::InvalidState;
    }

    void IOSAOTScriptingBackend::UnloadProjectAssembly() noexcept
    {
    }

    std::vector<ScriptTypeInfo> IOSAOTScriptingBackend::GetAvailableScriptTypes()
    {
        return {};
    }

    Result<ScriptInstanceHandle> IOSAOTScriptingBackend::CreateScriptInstance(const ScriptInstanceDesc& desc)
    {
        static_cast<void>(desc);
        return Result<ScriptInstanceHandle>::Failure(
            Error(initialized_ ? ErrorCode::Unsupported : ErrorCode::InvalidState, "iOS AOT scripting backend is not implemented yet."));
    }

    void IOSAOTScriptingBackend::DestroyScriptInstance(ScriptInstanceHandle script) noexcept
    {
        static_cast<void>(script);
    }

    void IOSAOTScriptingBackend::InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds) noexcept
    {
        static_cast<void>(script);
        static_cast<void>(event);
        static_cast<void>(deltaSeconds);
    }

    Result<std::string> IOSAOTScriptingBackend::GetScriptFieldsJson(ScriptInstanceHandle script)
    {
        static_cast<void>(script);
        return Result<std::string>::Failure(
            Error(initialized_ ? ErrorCode::Unsupported : ErrorCode::InvalidState, "iOS AOT scripting backend is not implemented yet."));
    }

    ErrorCode IOSAOTScriptingBackend::SetScriptFieldsJson(ScriptInstanceHandle script, std::string_view fieldsJson)
    {
        static_cast<void>(script);
        static_cast<void>(fieldsJson);
        return initialized_ ? ErrorCode::Unsupported : ErrorCode::InvalidState;
    }

    ErrorCode IOSAOTScriptingBackend::SetScriptFieldJson(ScriptInstanceHandle script, std::string_view fieldName, std::string_view valueJson)
    {
        static_cast<void>(script);
        static_cast<void>(fieldName);
        static_cast<void>(valueJson);
        return initialized_ ? ErrorCode::Unsupported : ErrorCode::InvalidState;
    }
} // namespace ve
