#pragma once

#include "Engine/Runtime/Scripting/DotNetScriptingBackend.h"

namespace ve
{
    class WindowsJITScriptingBackend final : public DotNetScriptingBackend
    {
    public:
        ~WindowsJITScriptingBackend() override;

        [[nodiscard]] ScriptingBackendType GetBackendType() const noexcept override;

    protected:
        [[nodiscard]] ErrorCode InitializeHost(const ScriptingSystemInitParam& initParam) override;
        void ShutdownHost() noexcept override;
        [[nodiscard]] ErrorCode LoadManagedEntryPoints(const ScriptingAssemblyLoadDesc& desc, ManagedScriptEntryPoints& entryPoints) override;

    private:
        void* hostFxrLibrary_ = nullptr;
        void* hostFxrContext_ = nullptr;
        void* loadAssemblyAndGetFunctionPointer_ = nullptr;
        void* closeHostFxr_ = nullptr;
    };
} // namespace ve
