#include "Engine/Runtime/Scripting/ScriptHost.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace ve
{
    namespace
    {
        constexpr std::string_view BootstrapTypeName = "VEngine.Scripting.ScriptApiBootstrap, VEngine.ScriptAPI";

        [[nodiscard]] Error MakeInvalidStateError(const char* message)
        {
            return Error(ErrorCode::InvalidState, message);
        }

        [[nodiscard]] Error MakeNotFoundError(std::string_view label, const Path& path)
        {
            return Error(ErrorCode::NotFound, std::string(label) + " was not found: " + path.GetString());
        }

        [[nodiscard]] std::string MakeStatusMessage(std::string_view action, std::int32_t status)
        {
            std::ostringstream stream;
            stream << action << " failed with status " << status << ".";
            return stream.str();
        }
    } // namespace

    ScriptHost::ScriptHost() = default;

    ScriptHost::~ScriptHost()
    {
        Shutdown();
    }

    Result<ScriptHostInfo> ScriptHost::Initialize(const ScriptHostDesc& desc)
    {
        if (initialized_)
        {
            return Result<ScriptHostInfo>::Failure(MakeInvalidStateError("ScriptHost is already initialized."));
        }

        if (desc.runtimeConfigPath.IsEmpty())
        {
            return Result<ScriptHostInfo>::Failure(
                Error(ErrorCode::InvalidArgument, "ScriptHost requires a runtimeconfig.json path."));
        }

        if (desc.scriptApiAssemblyPath.IsEmpty())
        {
            return Result<ScriptHostInfo>::Failure(
                Error(ErrorCode::InvalidArgument, "ScriptHost requires a VEngine.ScriptAPI assembly path."));
        }

        if (!FileSystem::IsFile(desc.runtimeConfigPath))
        {
            return Result<ScriptHostInfo>::Failure(MakeNotFoundError("ScriptHost runtimeconfig.json",
                                                                     desc.runtimeConfigPath));
        }

        if (!FileSystem::IsFile(desc.scriptApiAssemblyPath))
        {
            return Result<ScriptHostInfo>::Failure(MakeNotFoundError("VEngine.ScriptAPI assembly",
                                                                     desc.scriptApiAssemblyPath));
        }

        Result<DotNetHostInfo> dotNetInfo = dotNetHost_.Initialize(DotNetHostDesc{desc.runtimeConfigPath});
        if (!dotNetInfo)
        {
            return Result<ScriptHostInfo>::Failure(dotNetInfo.GetError());
        }

        Result<void*> initializeHost = LoadBootstrapFunction(desc.scriptApiAssemblyPath, "InitializeHost");
        Result<void*> loadProjectAssembly = LoadBootstrapFunction(desc.scriptApiAssemblyPath, "LoadProjectAssembly");
        Result<void*> createScriptInstance = LoadBootstrapFunction(desc.scriptApiAssemblyPath, "CreateScriptInstance");
        Result<void*> invokeLifecycle = LoadBootstrapFunction(desc.scriptApiAssemblyPath, "InvokeLifecycle");
        Result<void*> destroyScriptInstance = LoadBootstrapFunction(desc.scriptApiAssemblyPath, "DestroyScriptInstance");
        Result<void*> getLastError = LoadBootstrapFunction(desc.scriptApiAssemblyPath, "GetLastError");

        if (!initializeHost || !loadProjectAssembly || !createScriptInstance || !invokeLifecycle ||
            !destroyScriptInstance || !getLastError)
        {
            const Error error = !initializeHost      ? initializeHost.GetError()
                                : !loadProjectAssembly ? loadProjectAssembly.GetError()
                                : !createScriptInstance ? createScriptInstance.GetError()
                                : !invokeLifecycle      ? invokeLifecycle.GetError()
                                : !destroyScriptInstance ? destroyScriptInstance.GetError()
                                                         : getLastError.GetError();
            Shutdown();
            return Result<ScriptHostInfo>::Failure(error);
        }

        initializeHost_ = reinterpret_cast<InitializeHostFunction>(initializeHost.GetValue());
        loadProjectAssembly_ = reinterpret_cast<LoadProjectAssemblyFunction>(loadProjectAssembly.GetValue());
        createScriptInstance_ = reinterpret_cast<CreateScriptInstanceFunction>(createScriptInstance.GetValue());
        invokeLifecycle_ = reinterpret_cast<InvokeLifecycleFunction>(invokeLifecycle.GetValue());
        destroyScriptInstance_ = reinterpret_cast<DestroyScriptInstanceFunction>(destroyScriptInstance.GetValue());
        getLastError_ = reinterpret_cast<GetLastErrorFunction>(getLastError.GetValue());

        bridgeApi_ = CreateScriptBridgeApi(bridgeRegistry_);
        const std::int32_t initializeResult = initializeHost_(&bridgeApi_, static_cast<std::int32_t>(sizeof(bridgeApi_)));
        if (initializeResult != 0)
        {
            Error error = MakeManagedError("ScriptApiBootstrap.InitializeHost", initializeResult);
            Shutdown();
            return Result<ScriptHostInfo>::Failure(std::move(error));
        }

        initialized_ = true;

        ScriptHostInfo info;
        info.hostFxrPath = dotNetInfo.GetValue().hostFxrPath;
        return Result<ScriptHostInfo>::Success(std::move(info));
    }

    void ScriptHost::Shutdown() noexcept
    {
        initializeHost_ = nullptr;
        loadProjectAssembly_ = nullptr;
        createScriptInstance_ = nullptr;
        invokeLifecycle_ = nullptr;
        destroyScriptInstance_ = nullptr;
        getLastError_ = nullptr;
        bridgeRegistry_.Clear();
        dotNetHost_.Shutdown();
        initialized_ = false;
    }

    bool ScriptHost::IsInitialized() const noexcept
    {
        return initialized_;
    }

    ScriptBridgeRegistry& ScriptHost::GetBridgeRegistry() noexcept
    {
        return bridgeRegistry_;
    }

    const ScriptBridgeRegistry& ScriptHost::GetBridgeRegistry() const noexcept
    {
        return bridgeRegistry_;
    }

    Result<ScriptOperationResult> ScriptHost::LoadProjectAssembly(const Path& assemblyPath)
    {
        if (!initialized_ || loadProjectAssembly_ == nullptr)
        {
            return Result<ScriptOperationResult>::Failure(MakeInvalidStateError("ScriptHost is not initialized."));
        }

        if (assemblyPath.IsEmpty())
        {
            return Result<ScriptOperationResult>::Failure(
                Error(ErrorCode::InvalidArgument, "ScriptHost requires a project script assembly path."));
        }

        if (!FileSystem::IsFile(assemblyPath))
        {
            return Result<ScriptOperationResult>::Failure(MakeNotFoundError("Project script assembly", assemblyPath));
        }

        const std::string& assemblyPathText = assemblyPath.GetString();
        const std::int32_t result = loadProjectAssembly_(
            assemblyPathText.data(), static_cast<std::int32_t>(assemblyPathText.size()));
        if (result != 0)
        {
            return Result<ScriptOperationResult>::Failure(
                MakeManagedError("ScriptApiBootstrap.LoadProjectAssembly", result));
        }

        return Result<ScriptOperationResult>::Success(ScriptOperationResult{});
    }

    Result<ScriptInstanceId> ScriptHost::CreateScriptInstance(std::string_view scriptTypeName,
                                                              ScriptObjectHandle nativeHandle)
    {
        if (!initialized_ || createScriptInstance_ == nullptr)
        {
            return Result<ScriptInstanceId>::Failure(MakeInvalidStateError("ScriptHost is not initialized."));
        }

        if (scriptTypeName.empty())
        {
            return Result<ScriptInstanceId>::Failure(
                Error(ErrorCode::InvalidArgument, "ScriptHost requires a non-empty script type name."));
        }

        if (nativeHandle == InvalidScriptObjectHandle)
        {
            return Result<ScriptInstanceId>::Failure(
                Error(ErrorCode::InvalidArgument, "ScriptHost requires a valid native script handle."));
        }

        const ScriptInstanceId instanceId = createScriptInstance_(scriptTypeName.data(),
                                                                  static_cast<std::int32_t>(scriptTypeName.size()),
                                                                  nativeHandle);
        if (instanceId == InvalidScriptInstanceId)
        {
            return Result<ScriptInstanceId>::Failure(MakeManagedError("ScriptApiBootstrap.CreateScriptInstance", 0));
        }

        return Result<ScriptInstanceId>::Success(instanceId);
    }

    Result<ScriptOperationResult> ScriptHost::InvokeLifecycle(ScriptInstanceId instanceId,
                                                              ScriptLifecycleMethod lifecycle,
                                                              float deltaSeconds)
    {
        if (!initialized_ || invokeLifecycle_ == nullptr)
        {
            return Result<ScriptOperationResult>::Failure(MakeInvalidStateError("ScriptHost is not initialized."));
        }

        if (instanceId == InvalidScriptInstanceId)
        {
            return Result<ScriptOperationResult>::Failure(
                Error(ErrorCode::InvalidArgument, "ScriptHost requires a valid script instance id."));
        }

        const std::int32_t result =
            invokeLifecycle_(instanceId, static_cast<std::int32_t>(lifecycle), deltaSeconds);
        if (result != 0)
        {
            return Result<ScriptOperationResult>::Failure(
                MakeManagedError("ScriptApiBootstrap.InvokeLifecycle", result));
        }

        return Result<ScriptOperationResult>::Success(ScriptOperationResult{});
    }

    Result<ScriptOperationResult> ScriptHost::DestroyScriptInstance(ScriptInstanceId instanceId)
    {
        if (!initialized_ || destroyScriptInstance_ == nullptr)
        {
            return Result<ScriptOperationResult>::Failure(MakeInvalidStateError("ScriptHost is not initialized."));
        }

        if (instanceId == InvalidScriptInstanceId)
        {
            return Result<ScriptOperationResult>::Failure(
                Error(ErrorCode::InvalidArgument, "ScriptHost requires a valid script instance id."));
        }

        const std::int32_t result = destroyScriptInstance_(instanceId);
        if (result != 0)
        {
            return Result<ScriptOperationResult>::Failure(
                MakeManagedError("ScriptApiBootstrap.DestroyScriptInstance", result));
        }

        return Result<ScriptOperationResult>::Success(ScriptOperationResult{});
    }

    Result<void*> ScriptHost::LoadBootstrapFunction(const Path& assemblyPath, std::string_view methodName)
    {
        DotNetAssemblyFunctionDesc functionDesc;
        functionDesc.assemblyPath = assemblyPath;
        functionDesc.typeName = std::string(BootstrapTypeName);
        functionDesc.methodName = std::string(methodName);
        functionDesc.unmanagedCallersOnly = true;
        return dotNetHost_.LoadAssemblyFunction(functionDesc);
    }

    Error ScriptHost::MakeManagedError(std::string_view action, std::int32_t status) const
    {
        std::string message = ReadManagedLastError();
        if (message.empty())
        {
            message = MakeStatusMessage(action, status);
        }
        else
        {
            message = std::string(action) + " failed: " + message;
        }

        return Error(ErrorCode::PlatformError, std::move(message));
    }

    std::string ScriptHost::ReadManagedLastError() const
    {
        if (getLastError_ == nullptr)
        {
            return {};
        }

        const std::int32_t requiredSize = getLastError_(nullptr, 0);
        if (requiredSize <= 0)
        {
            return {};
        }

        std::vector<char> buffer(static_cast<std::size_t>(requiredSize) + 1, '\0');
        const std::int32_t copiedSize = getLastError_(buffer.data(), static_cast<std::int32_t>(buffer.size()));
        if (copiedSize <= 0)
        {
            return {};
        }

        return std::string(buffer.data(), static_cast<std::size_t>(std::min(requiredSize, copiedSize)));
    }
} // namespace ve
