#pragma once

#include "Engine/Runtime/Scripting/DotNetScriptingBackend.h"

namespace ve
{
    class IOSAOTScriptingBackend final : public DotNetScriptingBackend
    {
    public:
        [[nodiscard]] ErrorCode Initialize(const ScriptingSystemInitParam& initParam) override;
        void Shutdown() noexcept override;
        [[nodiscard]] ScriptingBackendType GetBackendType() const noexcept override;

        [[nodiscard]] ErrorCode LoadAssembly(const ScriptingAssemblyLoadDesc& desc) override;
        [[nodiscard]] ErrorCode LoadProjectAssembly(const ScriptingProjectAssemblyLoadDesc& desc) override;
        void UnloadProjectAssembly() noexcept override;
        [[nodiscard]] std::vector<ScriptTypeInfo> GetAvailableScriptTypes() override;
        [[nodiscard]] Result<ScriptInstanceHandle> CreateScriptInstance(const ScriptInstanceDesc& desc) override;
        void DestroyScriptInstance(ScriptInstanceHandle script) noexcept override;
        void InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds = 0.0f) noexcept override;
        [[nodiscard]] Result<std::string> GetScriptFieldsJson(ScriptInstanceHandle script) override;
        [[nodiscard]] ErrorCode SetScriptFieldsJson(ScriptInstanceHandle script, std::string_view fieldsJson) override;
        [[nodiscard]] ErrorCode SetScriptFieldJson(ScriptInstanceHandle script, std::string_view fieldName, std::string_view valueJson) override;

    private:
        bool initialized_ = false;
    };
} // namespace ve
