#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"
#include "Engine/Runtime/Scripting/DotNetHost.h"
#include "Engine/Runtime/Scripting/ScriptBridge.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace ve
{
#if defined(_WIN32)
#define VE_SCRIPT_HOST_CALLTYPE __stdcall
#else
#define VE_SCRIPT_HOST_CALLTYPE
#endif

    using ScriptInstanceId = std::uint64_t;

    inline constexpr ScriptInstanceId InvalidScriptInstanceId = 0;

    enum class ScriptLifecycleMethod : std::int32_t
    {
        OnCreate = 0,
        OnDestroy = 1,
        OnEnable = 2,
        OnDisable = 3,
        OnUpdate = 4,
        OnFixedUpdate = 5,
    };

    struct ScriptOperationResult
    {
    };

    struct ScriptHostDesc
    {
        Path runtimeConfigPath;
        Path scriptApiAssemblyPath;
    };

    struct ScriptHostInfo
    {
        Path hostFxrPath;
    };

    class ScriptHost : public NonMovable
    {
    public:
        ScriptHost();
        ~ScriptHost();

        [[nodiscard]] Result<ScriptHostInfo> Initialize(const ScriptHostDesc& desc);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] ScriptBridgeRegistry& GetBridgeRegistry() noexcept;
        [[nodiscard]] const ScriptBridgeRegistry& GetBridgeRegistry() const noexcept;

        [[nodiscard]] Result<ScriptOperationResult> LoadProjectAssembly(const Path& assemblyPath);
        [[nodiscard]] Result<ScriptOperationResult> UnloadProjectAssembly();
        [[nodiscard]] Result<ScriptInstanceId> CreateScriptInstance(std::string_view scriptTypeName,
                                                                    ScriptObjectHandle nativeHandle);
        [[nodiscard]] Result<ScriptOperationResult> InvokeLifecycle(ScriptInstanceId instanceId,
                                                                    ScriptLifecycleMethod lifecycle,
                                                                    float deltaSeconds);
        [[nodiscard]] Result<ScriptOperationResult> DestroyScriptInstance(ScriptInstanceId instanceId);

    private:
        using InitializeHostFunction =
            std::int32_t(VE_SCRIPT_HOST_CALLTYPE*)(const ScriptBridgeApi* bridgeApi, std::int32_t bridgeApiSize);
        using LoadProjectAssemblyFunction =
            std::int32_t(VE_SCRIPT_HOST_CALLTYPE*)(const char* assemblyPath, std::int32_t assemblyPathSizeInBytes);
        using CreateScriptInstanceFunction = ScriptInstanceId(VE_SCRIPT_HOST_CALLTYPE*)(
            const char* scriptTypeName, std::int32_t scriptTypeNameSizeInBytes, ScriptObjectHandle nativeHandle);
        using InvokeLifecycleFunction =
            std::int32_t(VE_SCRIPT_HOST_CALLTYPE*)(ScriptInstanceId instanceId,
                                                   std::int32_t lifecycle,
                                                   float deltaSeconds);
        using DestroyScriptInstanceFunction =
            std::int32_t(VE_SCRIPT_HOST_CALLTYPE*)(ScriptInstanceId instanceId);
        using UnloadProjectAssemblyFunction = std::int32_t(VE_SCRIPT_HOST_CALLTYPE*)();
        using GetLastErrorFunction =
            std::int32_t(VE_SCRIPT_HOST_CALLTYPE*)(char* buffer, std::int32_t bufferSizeInBytes);

        [[nodiscard]] Result<void*> LoadBootstrapFunction(const Path& assemblyPath, std::string_view methodName);
        [[nodiscard]] Error MakeManagedError(std::string_view action, std::int32_t status) const;
        [[nodiscard]] std::string ReadManagedLastError() const;

        DotNetHost dotNetHost_;
        ScriptBridgeRegistry bridgeRegistry_;
        ScriptBridgeApi bridgeApi_;
        InitializeHostFunction initializeHost_ = nullptr;
        LoadProjectAssemblyFunction loadProjectAssembly_ = nullptr;
        CreateScriptInstanceFunction createScriptInstance_ = nullptr;
        InvokeLifecycleFunction invokeLifecycle_ = nullptr;
        DestroyScriptInstanceFunction destroyScriptInstance_ = nullptr;
        UnloadProjectAssemblyFunction unloadProjectAssembly_ = nullptr;
        GetLastErrorFunction getLastError_ = nullptr;
        bool initialized_ = false;
    };

#undef VE_SCRIPT_HOST_CALLTYPE
} // namespace ve
