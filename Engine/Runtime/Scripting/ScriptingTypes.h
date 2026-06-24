#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>

namespace ve
{
    class ScriptableComponent;

    enum class ScriptingBackendType
    {
        Auto,
        WindowsJIT,
        IOSAOT,
    };

    enum class ScriptLifecycleEvent
    {
        OnCreate,
        OnDestroy,
        OnUpdate,
        OnLateUpdate,
        OnEnable,
        OnDisable,
    };

    struct ScriptingSystemInitParam
    {
        ScriptingBackendType backendType = ScriptingBackendType::Auto;
        Path dotNetRuntimeRoot;
        Path runtimeConfigPath;
    };

    struct ScriptingAssemblyLoadDesc
    {
        Path assemblyPath;
        std::string bridgeTypeName = "VEngine.Scripting.NativeScriptBridge, VEngine.ScriptAPI";
    };

    using ScriptInstanceHandle = UInt64;

    struct ScriptInstanceDesc
    {
        ScriptableComponent* component = nullptr;
        std::string typeName;
    };

    struct ManagedScriptEntryPoints
    {
        using CreateFn = ScriptInstanceHandle (*)(void* nativeComponent, const char* scriptTypeName);
        using DestroyFn = void (*)(ScriptInstanceHandle script);
        using UpdateFn = void (*)(ScriptInstanceHandle script, Float32 deltaSeconds);
        using EventFn = void (*)(ScriptInstanceHandle script);

        CreateFn create = nullptr;
        DestroyFn destroy = nullptr;
        UpdateFn update = nullptr;
        UpdateFn lateUpdate = nullptr;
        EventFn enable = nullptr;
        EventFn disable = nullptr;
    };
} // namespace ve
