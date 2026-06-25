#pragma once

#include "Engine/Runtime/Scripting/DotNetScriptingBackend.h"

#include <filesystem>

namespace ve
{
    class WindowsJITScriptingBackend final : public DotNetScriptingBackend
    {
    public:
        ~WindowsJITScriptingBackend() override;

        [[nodiscard]] ErrorCode Initialize(const ScriptingSystemInitParam& initParam) override;
        void Shutdown() noexcept override;
        [[nodiscard]] ScriptingBackendType GetBackendType() const noexcept override;

        [[nodiscard]] ErrorCode LoadAssembly(const ScriptingAssemblyLoadDesc& desc) override;
        [[nodiscard]] Result<ScriptInstanceHandle> CreateScriptInstance(const ScriptInstanceDesc& desc) override;
        void DestroyScriptInstance(ScriptInstanceHandle script) noexcept override;
        void InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds = 0.0f) noexcept override;

    private:
        [[nodiscard]] ErrorCode InitializeHost(const ScriptingSystemInitParam& initParam);
        void ShutdownHost() noexcept;
        [[nodiscard]] ErrorCode LoadManagedEntryPoints(const ScriptingAssemblyLoadDesc& desc, ManagedScriptEntryPoints& entryPoints);

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
