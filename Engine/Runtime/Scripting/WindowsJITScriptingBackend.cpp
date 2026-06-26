#include "Engine/Runtime/Scripting/WindowsJITScriptingBackend.h"

#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scene/GameObject.h"
#include "Engine/Runtime/Scene/TransformComponent.h"
#include "Engine/Runtime/Scripting/ScriptableComponent.h"

#include <algorithm>
#include <array>
#include <boost/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

#if VE_PLATFORM_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace ve
{
    namespace
    {
#if VE_PLATFORM_WINDOWS
        using HostFxrHandle = void*;
        using CharT = wchar_t;

        struct HostFxrInitializeParameters
        {
            size_t size;
            const CharT* hostPath;
            const CharT* dotnetRoot;
        };

        enum class HostFxrDelegateType
        {
            LoadAssemblyAndGetFunctionPointer = 5,
        };

        using HostFxrInitializeForRuntimeConfigFn = int (*)(const CharT* runtimeConfigPath, const HostFxrInitializeParameters* parameters, HostFxrHandle* hostContextHandle);
        using HostFxrGetRuntimeDelegateFn = int (*)(HostFxrHandle hostContextHandle, HostFxrDelegateType delegateType, void** delegate);
        using HostFxrCloseFn = int (*)(HostFxrHandle hostContextHandle);
        using LoadAssemblyAndGetFunctionPointerFn = int (*)(const CharT* assemblyPath,
                                                            const CharT* typeName,
                                                            const CharT* methodName,
                                                            const CharT* delegateTypeName,
                                                            void* reserved,
                                                            void** delegate);

        [[nodiscard]] const CharT* GetUnmanagedCallersOnlyMethod() noexcept
        {
            return reinterpret_cast<const CharT*>(-1);
        }

        [[nodiscard]] std::wstring Utf8ToWide(std::string_view text)
        {
            if (text.empty())
            {
                return {};
            }

            const int requiredSize = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
            if (requiredSize <= 0)
            {
                return {};
            }

            std::wstring result(static_cast<size_t>(requiredSize), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), requiredSize);
            return result;
        }

        [[nodiscard]] std::filesystem::path ToNativePath(const Path& path)
        {
            return std::filesystem::path(Utf8ToWide(path.GetString()));
        }

        [[nodiscard]] std::filesystem::path FindDotNetRuntimeRootFrom(std::filesystem::path start)
        {
            if (start.empty())
            {
                return {};
            }

            std::error_code error;
            start = std::filesystem::absolute(start, error);
            if (error)
            {
                return {};
            }

            if (std::filesystem::is_regular_file(start, error))
            {
                start = start.parent_path();
            }

            for (std::filesystem::path current = start; !current.empty(); current = current.parent_path())
            {
                const std::filesystem::path candidate = current / L"ThirdParty" / L"DotNet" / L"win-x64" / L"10.0.9";
                if (std::filesystem::exists(candidate / L"dotnet.exe", error) && std::filesystem::is_directory(candidate / L"host" / L"fxr", error))
                {
                    return candidate;
                }

                if (current == current.root_path())
                {
                    break;
                }
            }

            return {};
        }

        [[nodiscard]] std::filesystem::path ResolveDefaultRuntimeRoot()
        {
            const Path& projectRoot = FileSystem::GetProjectRoot();
            if (!projectRoot.IsEmpty())
            {
                std::filesystem::path runtimeRoot = FindDotNetRuntimeRootFrom(ToNativePath(projectRoot));
                if (!runtimeRoot.empty())
                {
                    return runtimeRoot;
                }
            }

            std::error_code error;
            std::filesystem::path runtimeRoot = FindDotNetRuntimeRootFrom(std::filesystem::current_path(error));
            if (!runtimeRoot.empty())
            {
                return runtimeRoot;
            }

            return FindDotNetRuntimeRootFrom(ToNativePath(FileSystem::GetExecutableDirectory()));
        }

        [[nodiscard]] std::filesystem::path ResolveHostFxrPath(const std::filesystem::path& runtimeRoot)
        {
            const std::filesystem::path pinnedHostFxrPath = runtimeRoot / L"host" / L"fxr" / L"10.0.9" / L"hostfxr.dll";
            std::error_code error;
            if (std::filesystem::exists(pinnedHostFxrPath, error))
            {
                return pinnedHostFxrPath;
            }

            const std::filesystem::path hostFxrRoot = runtimeRoot / L"host" / L"fxr";
            if (!std::filesystem::is_directory(hostFxrRoot, error))
            {
                return {};
            }

            std::vector<std::filesystem::path> candidates;
            for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(hostFxrRoot, error))
            {
                if (error)
                {
                    return {};
                }

                const std::filesystem::path candidate = entry.path() / L"hostfxr.dll";
                if (std::filesystem::exists(candidate, error))
                {
                    candidates.push_back(candidate);
                }
            }

            std::sort(candidates.begin(), candidates.end());
            return candidates.empty() ? std::filesystem::path() : candidates.back();
        }

        template<typename TFunction>
        [[nodiscard]] TFunction ResolveHostFxrExport(HMODULE library, const char* name)
        {
            return reinterpret_cast<TFunction>(GetProcAddress(library, name));
        }

        template<typename TDelegate>
        [[nodiscard]] ErrorCode LoadEntryPoint(LoadAssemblyAndGetFunctionPointerFn loadFunction,
                                               const std::wstring& assemblyPath,
                                               const std::wstring& typeName,
                                               const wchar_t* methodName,
                                               TDelegate& output)
        {
            void* function = nullptr;
            const int result = loadFunction(assemblyPath.c_str(), typeName.c_str(), methodName, GetUnmanagedCallersOnlyMethod(), nullptr, &function);
            if (result != 0 || function == nullptr)
            {
                return ErrorCode::InvalidState;
            }

            output = reinterpret_cast<TDelegate>(function);
            return ErrorCode::None;
        }
#endif

        void NativeGetTransformLocalPosition(void* nativeComponent, Float32* x, Float32* y, Float32* z)
        {
            if (x != nullptr)
            {
                *x = 0.0f;
            }
            if (y != nullptr)
            {
                *y = 0.0f;
            }
            if (z != nullptr)
            {
                *z = 0.0f;
            }

            auto* scriptComponent = static_cast<ScriptableComponent*>(nativeComponent);
            GameObject* owner = scriptComponent != nullptr ? scriptComponent->GetOwner() : nullptr;
            TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;
            if (transform == nullptr)
            {
                return;
            }

            const Vector3& position = transform->GetLocalPosition();
            if (x != nullptr)
            {
                *x = position.GetX();
            }
            if (y != nullptr)
            {
                *y = position.GetY();
            }
            if (z != nullptr)
            {
                *z = position.GetZ();
            }
        }

        void NativeSetTransformLocalPosition(void* nativeComponent, Float32 x, Float32 y, Float32 z)
        {
            auto* scriptComponent = static_cast<ScriptableComponent*>(nativeComponent);
            GameObject* owner = scriptComponent != nullptr ? scriptComponent->GetOwner() : nullptr;
            TransformComponent* transform = owner != nullptr ? owner->GetComponent<TransformComponent>() : nullptr;
            if (transform == nullptr)
            {
                return;
            }

            transform->SetLocalPosition(Vector3(x, y, z));
        }

        void NativeLogInfo(const char* text)
        {
            VE_LOG_INFO_CATEGORY("Script", "{}", text != nullptr ? text : "");
        }
    } // namespace

    WindowsJITScriptingBackend::~WindowsJITScriptingBackend()
    {
        Shutdown();
    }

    ErrorCode WindowsJITScriptingBackend::Initialize(const ScriptingSystemInitParam& initParam)
    {
        if (initialized_)
        {
            return ErrorCode::InvalidState;
        }

        const ErrorCode result = InitializeHost(initParam);
        if (result != ErrorCode::None)
        {
            return result;
        }

        initialized_ = true;
        return ErrorCode::None;
    }

    void WindowsJITScriptingBackend::Shutdown() noexcept
    {
        if (!initialized_)
        {
            return;
        }

        UnloadProjectAssembly();
        entryPoints_ = {};
        assemblyLoaded_ = false;
        initialized_ = false;
        ShutdownHost();
        runtimeRoot_.clear();
        runtimeConfigPath_.clear();
    }

    ScriptingBackendType WindowsJITScriptingBackend::GetBackendType() const noexcept
    {
        return ScriptingBackendType::WindowsJIT;
    }

    ErrorCode WindowsJITScriptingBackend::LoadAssembly(const ScriptingAssemblyLoadDesc& desc)
    {
        if (!initialized_)
        {
            return ErrorCode::InvalidState;
        }

        if (desc.assemblyPath.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        ManagedScriptEntryPoints entryPoints;
        const ErrorCode result = LoadManagedEntryPoints(desc, entryPoints);
        if (result != ErrorCode::None)
        {
            return result;
        }

        if (entryPoints.create == nullptr || entryPoints.destroy == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        entryPoints_ = entryPoints;
        assemblyLoaded_ = true;
        if (entryPoints_.registerNativeApi != nullptr)
        {
            entryPoints_.registerNativeApi(reinterpret_cast<void*>(&NativeGetTransformLocalPosition),
                                           reinterpret_cast<void*>(&NativeSetTransformLocalPosition),
                                           reinterpret_cast<void*>(&NativeLogInfo));
        }
        VE_LOG_INFO_CATEGORY("Script", "Loaded managed script assembly {}.", desc.assemblyPath.GetString());
        return ErrorCode::None;
    }

    ErrorCode WindowsJITScriptingBackend::LoadProjectAssembly(const ScriptingProjectAssemblyLoadDesc& desc)
    {
        if (!initialized_ || !assemblyLoaded_ || entryPoints_.loadProjectAssembly == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        if (desc.assemblyPath.IsEmpty())
        {
            return ErrorCode::InvalidArgument;
        }

        const std::filesystem::path assemblyPath = ToNativePath(desc.assemblyPath);
        std::error_code error;
        if (!std::filesystem::exists(assemblyPath, error))
        {
            return ErrorCode::NotFound;
        }

        const int result = entryPoints_.loadProjectAssembly(desc.assemblyPath.GetString().c_str());
        if (result != 0)
        {
            return ErrorCode::InvalidState;
        }

        VE_LOG_INFO_CATEGORY("Script", "Loaded project script assembly {}.", desc.assemblyPath.GetString());
        return ErrorCode::None;
    }

    void WindowsJITScriptingBackend::UnloadProjectAssembly() noexcept
    {
        if (entryPoints_.unloadProjectAssembly != nullptr)
        {
            entryPoints_.unloadProjectAssembly();
        }
    }

    std::vector<ScriptTypeInfo> WindowsJITScriptingBackend::GetAvailableScriptTypes()
    {
        std::vector<ScriptTypeInfo> scriptTypes;
        if (entryPoints_.getScriptTypesJson == nullptr)
        {
            return scriptTypes;
        }

        const char* jsonText = entryPoints_.getScriptTypesJson();
        if (jsonText == nullptr)
        {
            return scriptTypes;
        }

        Result<boost::json::value> json = JsonUtils::Parse(jsonText);
        if (entryPoints_.freeString != nullptr)
        {
            entryPoints_.freeString(jsonText);
        }

        if (!json || !json.GetValue().is_array())
        {
            return scriptTypes;
        }

        for (const boost::json::value& value : json.GetValue().as_array())
        {
            if (!value.is_object())
            {
                continue;
            }

            const boost::json::object& object = value.as_object();
            const boost::json::value* typeName = object.if_contains("typeName");
            if (typeName == nullptr || !typeName->is_string())
            {
                continue;
            }

            ScriptTypeInfo info;
            info.typeName = std::string(typeName->as_string());
            if (const boost::json::value* displayName = object.if_contains("displayName"); displayName != nullptr && displayName->is_string())
            {
                info.displayName = std::string(displayName->as_string());
            }
            else
            {
                info.displayName = info.typeName;
            }
            scriptTypes.push_back(std::move(info));
        }

        return scriptTypes;
    }

    Result<ScriptInstanceHandle> WindowsJITScriptingBackend::CreateScriptInstance(const ScriptInstanceDesc& desc)
    {
        if (!initialized_ || !assemblyLoaded_)
        {
            return Result<ScriptInstanceHandle>::Failure(Error(ErrorCode::InvalidState, "Managed script assembly is not loaded."));
        }

        if (desc.component == nullptr || desc.typeName.empty())
        {
            return Result<ScriptInstanceHandle>::Failure(Error(ErrorCode::InvalidArgument, "Script instance creation requires a component and script type name."));
        }

        ScriptInstanceHandle handle = entryPoints_.create(desc.component, desc.typeName.c_str());
        if (handle == 0)
        {
            VE_LOG_ERROR_CATEGORY("Script", "Managed script bridge returned an empty handle for '{}'.", desc.typeName);
            return Result<ScriptInstanceHandle>::Failure(Error(ErrorCode::InvalidState, "Managed script instance creation returned an empty handle."));
        }

        return Result<ScriptInstanceHandle>::Success(handle);
    }

    void WindowsJITScriptingBackend::DestroyScriptInstance(ScriptInstanceHandle script) noexcept
    {
        if (script == 0 || entryPoints_.destroy == nullptr)
        {
            return;
        }

        entryPoints_.destroy(script);
    }

    void WindowsJITScriptingBackend::InvokeScriptEvent(ScriptInstanceHandle script, ScriptLifecycleEvent event, Float32 deltaSeconds) noexcept
    {
        if (script == 0)
        {
            return;
        }

        switch (event)
        {
        case ScriptLifecycleEvent::OnCreate:
            break;
        case ScriptLifecycleEvent::OnDestroy:
            DestroyScriptInstance(script);
            break;
        case ScriptLifecycleEvent::OnUpdate:
            if (entryPoints_.update != nullptr)
            {
                entryPoints_.update(script, deltaSeconds);
            }
            break;
        case ScriptLifecycleEvent::OnLateUpdate:
            if (entryPoints_.lateUpdate != nullptr)
            {
                entryPoints_.lateUpdate(script, deltaSeconds);
            }
            break;
        case ScriptLifecycleEvent::OnEnable:
            if (entryPoints_.enable != nullptr)
            {
                entryPoints_.enable(script);
            }
            break;
        case ScriptLifecycleEvent::OnDisable:
            if (entryPoints_.disable != nullptr)
            {
                entryPoints_.disable(script);
            }
            break;
        }
    }

    ErrorCode WindowsJITScriptingBackend::InitializeHost(const ScriptingSystemInitParam& initParam)
    {
#if !VE_PLATFORM_WINDOWS
        static_cast<void>(initParam);
        return ErrorCode::Unsupported;
#else
        std::filesystem::path runtimeRoot = initParam.dotNetRuntimeRoot.IsEmpty() ? ResolveDefaultRuntimeRoot() : ToNativePath(initParam.dotNetRuntimeRoot);
        std::error_code error;
        if (runtimeRoot.empty() || !std::filesystem::exists(runtimeRoot / L"dotnet.exe", error))
        {
            VE_LOG_ERROR_CATEGORY("Script", "Failed to locate .NET runtime root. Requested root: '{}'.", runtimeRoot.string());
            return ErrorCode::NotFound;
        }

        std::filesystem::path runtimeConfigPath;
        if (!initParam.runtimeConfigPath.IsEmpty())
        {
            runtimeConfigPath = ToNativePath(initParam.runtimeConfigPath);
            if (!std::filesystem::exists(runtimeConfigPath, error))
            {
                VE_LOG_ERROR_CATEGORY("Script", ".NET runtimeconfig was not found: '{}'.", runtimeConfigPath.string());
                return ErrorCode::NotFound;
            }
        }

        const std::filesystem::path hostFxrPath = ResolveHostFxrPath(runtimeRoot);
        if (hostFxrPath.empty())
        {
            VE_LOG_ERROR_CATEGORY("Script", "hostfxr.dll was not found under .NET runtime root '{}'.", runtimeRoot.string());
            return ErrorCode::NotFound;
        }

        HMODULE hostFxrLibrary = LoadLibraryW(hostFxrPath.c_str());
        if (hostFxrLibrary == nullptr)
        {
            VE_LOG_ERROR_CATEGORY("Script", "Failed to load hostfxr.dll '{}' with Win32 error {}.", hostFxrPath.string(), GetLastError());
            return ErrorCode::PlatformError;
        }

        auto initializeForRuntimeConfig =
            ResolveHostFxrExport<HostFxrInitializeForRuntimeConfigFn>(hostFxrLibrary, "hostfxr_initialize_for_runtime_config");
        auto getRuntimeDelegate = ResolveHostFxrExport<HostFxrGetRuntimeDelegateFn>(hostFxrLibrary, "hostfxr_get_runtime_delegate");
        auto closeHostFxr = ResolveHostFxrExport<HostFxrCloseFn>(hostFxrLibrary, "hostfxr_close");
        if (initializeForRuntimeConfig == nullptr || getRuntimeDelegate == nullptr || closeHostFxr == nullptr)
        {
            VE_LOG_ERROR_CATEGORY("Script", "hostfxr.dll '{}' is missing required exports.", hostFxrPath.string());
            FreeLibrary(hostFxrLibrary);
            return ErrorCode::InvalidState;
        }

        HostFxrHandle hostContext = nullptr;
        void* loadAssemblyAndGetFunctionPointer = nullptr;
        if (!runtimeConfigPath.empty())
        {
            VE_LOG_INFO_CATEGORY("Script",
                                 "Initializing .NET host. runtimeRoot='{}', runtimeConfig='{}', hostfxr='{}'.",
                                 runtimeRoot.string(),
                                 runtimeConfigPath.string(),
                                 hostFxrPath.string());
            int result = initializeForRuntimeConfig(runtimeConfigPath.c_str(), nullptr, &hostContext);
            if (result != 0 || hostContext == nullptr)
            {
                VE_LOG_WARN_CATEGORY("Script",
                                     "hostfxr_initialize_for_runtime_config failed with result {} without explicit dotnetRoot. Retrying with runtimeRoot='{}'.",
                                     result,
                                     runtimeRoot.string());
                const std::wstring runtimeRootText = runtimeRoot.wstring();
                const HostFxrInitializeParameters parameters{
                    sizeof(HostFxrInitializeParameters),
                    nullptr,
                    runtimeRootText.c_str(),
                };

                result = initializeForRuntimeConfig(runtimeConfigPath.c_str(), &parameters, &hostContext);
                if (result != 0 || hostContext == nullptr)
                {
                    VE_LOG_ERROR_CATEGORY("Script",
                                          "hostfxr_initialize_for_runtime_config failed with result {}. runtimeConfig='{}', runtimeRoot='{}'.",
                                          result,
                                          runtimeConfigPath.string(),
                                          runtimeRoot.string());
                    FreeLibrary(hostFxrLibrary);
                    return ErrorCode::InvalidState;
                }
            }

            result = getRuntimeDelegate(hostContext, HostFxrDelegateType::LoadAssemblyAndGetFunctionPointer, &loadAssemblyAndGetFunctionPointer);
            if (result != 0 || loadAssemblyAndGetFunctionPointer == nullptr)
            {
                VE_LOG_ERROR_CATEGORY("Script", "hostfxr_get_runtime_delegate failed with result {}.", result);
                closeHostFxr(hostContext);
                FreeLibrary(hostFxrLibrary);
                return ErrorCode::InvalidState;
            }
        }

        hostFxrLibrary_ = hostFxrLibrary;
        hostFxrContext_ = hostContext;
        loadAssemblyAndGetFunctionPointer_ = loadAssemblyAndGetFunctionPointer;
        closeHostFxr_ = reinterpret_cast<void*>(closeHostFxr);
        runtimeRoot_ = std::move(runtimeRoot);
        runtimeConfigPath_ = std::move(runtimeConfigPath);

        VE_LOG_INFO_CATEGORY("Script", "Initialized Windows JIT .NET host from {}.", hostFxrPath.string());
        return ErrorCode::None;
#endif
    }

    void WindowsJITScriptingBackend::ShutdownHost() noexcept
    {
#if VE_PLATFORM_WINDOWS
        if (hostFxrContext_ != nullptr && closeHostFxr_ != nullptr)
        {
            reinterpret_cast<HostFxrCloseFn>(closeHostFxr_)(hostFxrContext_);
        }

        if (hostFxrLibrary_ != nullptr)
        {
            FreeLibrary(reinterpret_cast<HMODULE>(hostFxrLibrary_));
        }
#endif

        hostFxrLibrary_ = nullptr;
        hostFxrContext_ = nullptr;
        loadAssemblyAndGetFunctionPointer_ = nullptr;
        closeHostFxr_ = nullptr;
    }

    ErrorCode WindowsJITScriptingBackend::LoadManagedEntryPoints(const ScriptingAssemblyLoadDesc& desc, ManagedScriptEntryPoints& entryPoints)
    {
#if !VE_PLATFORM_WINDOWS
        static_cast<void>(desc);
        static_cast<void>(entryPoints);
        return ErrorCode::Unsupported;
#else
        if (loadAssemblyAndGetFunctionPointer_ == nullptr || runtimeConfigPath_.empty())
        {
            return ErrorCode::InvalidState;
        }

        const std::filesystem::path assemblyPath = ToNativePath(desc.assemblyPath);
        std::error_code error;
        if (!std::filesystem::exists(assemblyPath, error))
        {
            return ErrorCode::NotFound;
        }

        const std::wstring bridgeTypeName = Utf8ToWide(desc.bridgeTypeName);
        if (bridgeTypeName.empty())
        {
            return ErrorCode::InvalidArgument;
        }

        auto loadFunction = reinterpret_cast<LoadAssemblyAndGetFunctionPointerFn>(loadAssemblyAndGetFunctionPointer_);
        ManagedScriptEntryPoints loadedEntryPoints;

        ErrorCode result = LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"RegisterNativeApi", loadedEntryPoints.registerNativeApi);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"LoadProjectAssembly", loadedEntryPoints.loadProjectAssembly);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"UnloadProjectAssembly", loadedEntryPoints.unloadProjectAssembly);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"GetScriptTypesJson", loadedEntryPoints.getScriptTypesJson);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"FreeString", loadedEntryPoints.freeString);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"CreateScript", loadedEntryPoints.create);
        if (result != ErrorCode::None)
        {
            return result;
        }

        result = LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"DestroyScript", loadedEntryPoints.destroy);
        if (result != ErrorCode::None)
        {
            return result;
        }

        static_cast<void>(LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"OnUpdate", loadedEntryPoints.update));
        static_cast<void>(LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"OnLateUpdate", loadedEntryPoints.lateUpdate));
        static_cast<void>(LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"OnEnable", loadedEntryPoints.enable));
        static_cast<void>(LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"OnDisable", loadedEntryPoints.disable));

        entryPoints = loadedEntryPoints;
        return ErrorCode::None;
#endif
    }
} // namespace ve
