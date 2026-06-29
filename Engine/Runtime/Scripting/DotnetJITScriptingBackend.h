#pragma once

#include "Engine/Runtime/Scripting/DotNetScriptingBackend.h"

#include <filesystem>
#include <string_view>
#include <vector>

namespace ve
{
    class DotnetJITScriptingBackend final : public DotNetScriptingBackend
    {
    public:
        ~DotnetJITScriptingBackend() override;

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
        [[nodiscard]] ErrorCode InitializeHost(const ScriptingSystemInitParam& initParam);
        void ShutdownHost() noexcept;

    private:
        std::filesystem::path runtimeRoot_;
        std::filesystem::path runtimeConfigPath_;
        ManagedScriptEntryPoints entryPoints_;
        void* hostFxrLibrary_ = nullptr;
        void* hostFxrContext_ = nullptr;
        void* loadAssemblyAndGetFunctionPointer_ = nullptr;
        void* closeHostFxr_ = nullptr;
        bool initialized_ = false;
        bool assemblyLoaded_ = false;
    };
} // namespace ve
