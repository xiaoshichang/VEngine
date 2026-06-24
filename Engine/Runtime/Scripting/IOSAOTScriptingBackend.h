#pragma once

#include "Engine/Runtime/Scripting/ScriptingSystemBackend.h"

namespace ve
{
    class IOSAOTScriptingBackend final : public ScriptingSystemBackend
    {
    public:
        [[nodiscard]] ErrorCode Initialize(const ScriptingSystemInitParam& initParam) override;
        void Shutdown() noexcept override;
        [[nodiscard]] ScriptingBackendType GetBackendType() const noexcept override;

        [[nodiscard]] ErrorCode LoadAssembly(const ScriptingAssemblyLoadDesc& desc) override;
        [[nodiscard]] Result<ScriptInstanceHandle> CreateScriptInstance(const ScriptInstanceDesc& desc) override;
        void DestroyScriptInstance(ScriptInstanceHandle script) noexcept override;
        void InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds = 0.0f) noexcept override;

    private:
        bool initialized_ = false;
    };
} // namespace ve
