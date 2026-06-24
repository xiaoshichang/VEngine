#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Scripting/ScriptingTypes.h"

namespace ve
{
    class ScriptingSystemBackend : public NonMovable
    {
    public:
        virtual ~ScriptingSystemBackend() = default;

        [[nodiscard]] virtual ErrorCode Initialize(const ScriptingSystemInitParam& initParam) = 0;
        virtual void Shutdown() noexcept = 0;
        [[nodiscard]] virtual ScriptingBackendType GetBackendType() const noexcept = 0;

        [[nodiscard]] virtual ErrorCode LoadAssembly(const ScriptingAssemblyLoadDesc& desc) = 0;
        [[nodiscard]] virtual Result<ScriptInstanceHandle> CreateScriptInstance(const ScriptInstanceDesc& desc) = 0;
        virtual void DestroyScriptInstance(ScriptInstanceHandle script) noexcept = 0;
        virtual void InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds = 0.0f) noexcept = 0;
    };
} // namespace ve
