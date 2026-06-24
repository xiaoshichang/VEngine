#pragma once

#include "Engine/Runtime/Scripting/ScriptingSystemBackend.h"

#include <filesystem>

namespace ve
{
    class DotNetScriptingBackend : public ScriptingSystemBackend
    {
    public:
        ~DotNetScriptingBackend() override;

        [[nodiscard]] ErrorCode Initialize(const ScriptingSystemInitParam& initParam) override;
        void Shutdown() noexcept override;
        [[nodiscard]] ErrorCode LoadAssembly(const ScriptingAssemblyLoadDesc& desc) override;
        [[nodiscard]] Result<ScriptInstanceHandle> CreateScriptInstance(const ScriptInstanceDesc& desc) override;
        void DestroyScriptInstance(ScriptInstanceHandle script) noexcept override;
        void InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds = 0.0f) noexcept override;

    protected:
        [[nodiscard]] virtual ErrorCode InitializeHost(const ScriptingSystemInitParam& initParam) = 0;
        virtual void ShutdownHost() noexcept = 0;
        [[nodiscard]] virtual ErrorCode LoadManagedEntryPoints(const ScriptingAssemblyLoadDesc& desc, ManagedScriptEntryPoints& entryPoints) = 0;

        [[nodiscard]] const std::filesystem::path& GetRuntimeRoot() const noexcept;
        [[nodiscard]] const std::filesystem::path& GetRuntimeConfigPath() const noexcept;
        void SetResolvedRuntimePaths(std::filesystem::path runtimeRoot, std::filesystem::path runtimeConfigPath);

    private:
        std::filesystem::path runtimeRoot_;
        std::filesystem::path runtimeConfigPath_;
        ManagedScriptEntryPoints entryPoints_;
        bool initialized_ = false;
        bool assemblyLoaded_ = false;
    };
} // namespace ve
