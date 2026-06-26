#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Scripting/ScriptingTypes.h"

#include <filesystem>

namespace ve
{
    struct ManagedScriptBindingInitParam
    {
        void* loadAssemblyAndGetFunctionPointer = nullptr;
        std::filesystem::path runtimeConfigPath;
    };

    [[nodiscard]] ErrorCode LoadManagedEntryPoints(const ScriptingAssemblyLoadDesc& desc,
                                                   const ManagedScriptBindingInitParam& initParam,
                                                   ManagedScriptEntryPoints& entryPoints);
    void RegisterNativeScriptApi(const ManagedScriptEntryPoints& entryPoints);
} // namespace ve
