#pragma once

#include "Engine/Runtime/Scripting/ScriptingSystemBackend.h"

namespace ve
{
    class DotNetScriptingBackend : public ScriptingSystemBackend
    {
    public:
        ~DotNetScriptingBackend() override = default;

        [[nodiscard]] virtual ErrorCode Initialize(const ScriptingSystemInitParam& initParam) override = 0;
        virtual void Shutdown() noexcept override = 0;
        [[nodiscard]] virtual ScriptingBackendType GetBackendType() const noexcept override = 0;
        [[nodiscard]] virtual ErrorCode LoadAssembly(const ScriptingAssemblyLoadDesc& desc) override = 0;
        [[nodiscard]] virtual Result<ScriptInstanceHandle> CreateScriptInstance(const ScriptInstanceDesc& desc) override = 0;
        virtual void DestroyScriptInstance(ScriptInstanceHandle script) noexcept override = 0;
        virtual void InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds = 0.0f) noexcept override = 0;
    };
} // namespace ve
