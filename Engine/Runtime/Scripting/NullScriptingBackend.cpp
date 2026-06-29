#include "Engine/Runtime/Scripting/NullScriptingBackend.h"

namespace ve
{
    ErrorCode NullScriptingBackend::Initialize(const ScriptingSystemInitParam&)
    {
        return ErrorCode::None;
    }

    void NullScriptingBackend::Shutdown() noexcept
    {
    }

    ScriptingBackendType NullScriptingBackend::GetBackendType() const noexcept
    {
        return ScriptingBackendType::Auto;
    }

    ErrorCode NullScriptingBackend::LoadAssembly(const ScriptingAssemblyLoadDesc&)
    {
        return ErrorCode::Unsupported;
    }

    ErrorCode NullScriptingBackend::LoadProjectAssembly(const ScriptingProjectAssemblyLoadDesc&)
    {
        return ErrorCode::Unsupported;
    }

    void NullScriptingBackend::UnloadProjectAssembly() noexcept
    {
    }

    std::vector<ScriptTypeInfo> NullScriptingBackend::GetAvailableScriptTypes()
    {
        return {};
    }

    Result<ScriptInstanceHandle> NullScriptingBackend::CreateScriptInstance(const ScriptInstanceDesc&)
    {
        return Result<ScriptInstanceHandle>::Failure(Error(ErrorCode::Unsupported, "Scripting is not available on this platform."));
    }

    void NullScriptingBackend::DestroyScriptInstance(ScriptInstanceHandle) noexcept
    {
    }

    void NullScriptingBackend::InvokeScriptEvent(ScriptInstanceHandle, ScriptLifecycleEvent, Float32) noexcept
    {
    }

    Result<std::string> NullScriptingBackend::GetScriptFieldsJson(ScriptInstanceHandle)
    {
        return Result<std::string>::Failure(Error(ErrorCode::Unsupported, "Scripting is not available on this platform."));
    }

    ErrorCode NullScriptingBackend::SetScriptFieldsJson(ScriptInstanceHandle, std::string_view)
    {
        return ErrorCode::Unsupported;
    }

    ErrorCode NullScriptingBackend::SetScriptFieldJson(ScriptInstanceHandle, std::string_view, std::string_view)
    {
        return ErrorCode::Unsupported;
    }
} // namespace ve
