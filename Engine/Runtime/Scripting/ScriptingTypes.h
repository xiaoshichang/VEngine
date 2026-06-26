#pragma once

#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <string>
#include <vector>

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
        std::string bridgeTypeName = "VEngine.Scripting.NativeScriptBridge, VEngine.ScriptHost";
    };

    struct ScriptingProjectAssemblyLoadDesc
    {
        Path assemblyPath;
    };

    struct ScriptTypeInfo
    {
        std::string typeName;
        std::string displayName;
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
        using RegisterNativeApiFn = void (*)(void* getTransformLocalPosition, void* setTransformLocalPosition, void* logInfo);
        using LoadProjectAssemblyFn = int (*)(const char* assemblyPath);
        using UnloadProjectAssemblyFn = void (*)();
        using GetScriptTypesJsonFn = const char* (*)();
        using FreeStringFn = void (*)(const char* text);

        RegisterNativeApiFn registerNativeApi = nullptr;
        LoadProjectAssemblyFn loadProjectAssembly = nullptr;
        UnloadProjectAssemblyFn unloadProjectAssembly = nullptr;
        GetScriptTypesJsonFn getScriptTypesJson = nullptr;
        FreeStringFn freeString = nullptr;
        CreateFn create = nullptr;
        DestroyFn destroy = nullptr;
        UpdateFn update = nullptr;
        UpdateFn lateUpdate = nullptr;
        EventFn enable = nullptr;
        EventFn disable = nullptr;
    };
} // namespace ve
