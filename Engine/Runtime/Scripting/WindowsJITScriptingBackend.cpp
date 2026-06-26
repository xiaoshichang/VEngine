#include "Engine/Runtime/Scripting/WindowsJITScriptingBackend.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/JsonUtils.h"
#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Scripting/Binding/NativeScriptBinding.h"

#include <algorithm>
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

        struct HostRuntimePaths
        {
            std::filesystem::path runtimeRoot;
            std::filesystem::path runtimeConfigPath;
            std::filesystem::path hostFxrPath;
        };

        struct HostFxrExports
        {
            HMODULE library = nullptr;
            HostFxrInitializeForRuntimeConfigFn initializeForRuntimeConfig = nullptr;
            HostFxrGetRuntimeDelegateFn getRuntimeDelegate = nullptr;
            HostFxrCloseFn close = nullptr;
        };

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

        [[nodiscard]] ErrorCode FailHostInitialization(ErrorCode code, const char* assertionMessage)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, assertionMessage);
            return code;
        }

        [[nodiscard]] ErrorCode ResolveHostRuntimePaths(const ScriptingSystemInitParam& initParam, HostRuntimePaths& paths)
        {
            // Step 1: resolve the app-local or project-local .NET runtime that owns hostfxr.
            std::filesystem::path runtimeRoot = initParam.dotNetRuntimeRoot.IsEmpty() ? ResolveDefaultRuntimeRoot() : ToNativePath(initParam.dotNetRuntimeRoot);
            std::error_code error;
            if (runtimeRoot.empty() || !std::filesystem::exists(runtimeRoot / L"dotnet.exe", error))
            {
                VE_LOG_ERROR_CATEGORY("Script", "Failed to locate .NET runtime root. Requested root: '{}'.", runtimeRoot.string());
                return FailHostInitialization(ErrorCode::NotFound, "WindowsJITScriptingBackend requires a valid .NET runtime root.");
            }

            // Step 2: native hosting should initialize CoreCLR from the managed host runtimeconfig.
            if (initParam.runtimeConfigPath.IsEmpty())
            {
                VE_LOG_ERROR_CATEGORY("Script", ".NET runtimeconfig path is required for Windows JIT scripting.");
                return FailHostInitialization(ErrorCode::InvalidArgument, "WindowsJITScriptingBackend requires a .NET runtimeconfig path.");
            }

            std::filesystem::path runtimeConfigPath = ToNativePath(initParam.runtimeConfigPath);
            if (!std::filesystem::exists(runtimeConfigPath, error))
            {
                VE_LOG_ERROR_CATEGORY("Script", ".NET runtimeconfig was not found: '{}'.", runtimeConfigPath.string());
                return FailHostInitialization(ErrorCode::NotFound, "WindowsJITScriptingBackend runtimeconfig path does not exist.");
            }

            // Step 3: load hostfxr from the chosen runtime instead of relying on machine-global state.
            const std::filesystem::path hostFxrPath = ResolveHostFxrPath(runtimeRoot);
            if (hostFxrPath.empty())
            {
                VE_LOG_ERROR_CATEGORY("Script", "hostfxr.dll was not found under .NET runtime root '{}'.", runtimeRoot.string());
                return FailHostInitialization(ErrorCode::NotFound, "WindowsJITScriptingBackend could not find hostfxr.dll.");
            }

            paths.runtimeRoot = std::move(runtimeRoot);
            paths.runtimeConfigPath = std::move(runtimeConfigPath);
            paths.hostFxrPath = std::move(hostFxrPath);
            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode LoadHostFxrExports(const std::filesystem::path& hostFxrPath, HostFxrExports& exports)
        {
            // Step 4: load hostfxr and resolve only the exports used by the prescribed hosting flow.
            HMODULE library = LoadLibraryW(hostFxrPath.c_str());
            if (library == nullptr)
            {
                VE_LOG_ERROR_CATEGORY("Script", "Failed to load hostfxr.dll '{}' with Win32 error {}.", hostFxrPath.string(), GetLastError());
                return FailHostInitialization(ErrorCode::PlatformError, "WindowsJITScriptingBackend failed to load hostfxr.dll.");
            }

            HostFxrExports loadedExports;
            loadedExports.library = library;
            loadedExports.initializeForRuntimeConfig =
                ResolveHostFxrExport<HostFxrInitializeForRuntimeConfigFn>(library, "hostfxr_initialize_for_runtime_config");
            loadedExports.getRuntimeDelegate = ResolveHostFxrExport<HostFxrGetRuntimeDelegateFn>(library, "hostfxr_get_runtime_delegate");
            loadedExports.close = ResolveHostFxrExport<HostFxrCloseFn>(library, "hostfxr_close");
            if (loadedExports.initializeForRuntimeConfig == nullptr || loadedExports.getRuntimeDelegate == nullptr || loadedExports.close == nullptr)
            {
                VE_LOG_ERROR_CATEGORY("Script", "hostfxr.dll '{}' is missing required exports.", hostFxrPath.string());
                FreeLibrary(library);
                return FailHostInitialization(ErrorCode::InvalidState, "WindowsJITScriptingBackend hostfxr.dll is missing required exports.");
            }

            exports = loadedExports;
            return ErrorCode::None;
        }

        [[nodiscard]] ErrorCode InitializeRuntimeHost(const HostRuntimePaths& paths,
                                                      const HostFxrExports& exports,
                                                      HostFxrHandle& hostContext,
                                                      void*& loadAssemblyAndGetFunctionPointer)
        {
            // Step 5: follow hostfxr's normal runtimeconfig-first initialization path.
            VE_LOG_INFO_CATEGORY("Script",
                                 "Initializing .NET host. runtimeRoot='{}', runtimeConfig='{}', hostfxr='{}'.",
                                 paths.runtimeRoot.string(),
                                 paths.runtimeConfigPath.string(),
                                 paths.hostFxrPath.string());

            HostFxrHandle initializedContext = nullptr;
            int result = exports.initializeForRuntimeConfig(paths.runtimeConfigPath.c_str(), nullptr, &initializedContext);
            if (result != 0 || initializedContext == nullptr)
            {
                VE_LOG_ERROR_CATEGORY("Script",
                                      "hostfxr_initialize_for_runtime_config failed with result {}. runtimeConfig='{}', runtimeRoot='{}'.",
                                      result,
                                      paths.runtimeConfigPath.string(),
                                      paths.runtimeRoot.string());
                return FailHostInitialization(ErrorCode::InvalidState, "WindowsJITScriptingBackend failed to initialize hostfxr from runtimeconfig.");
            }

            // Step 6: get the unmanaged entry-point loader used for the managed bridge methods.
            void* loadedDelegate = nullptr;
            result = exports.getRuntimeDelegate(initializedContext, HostFxrDelegateType::LoadAssemblyAndGetFunctionPointer, &loadedDelegate);
            if (result != 0 || loadedDelegate == nullptr)
            {
                VE_LOG_ERROR_CATEGORY("Script", "hostfxr_get_runtime_delegate failed with result {}.", result);
                exports.close(initializedContext);
                return FailHostInitialization(ErrorCode::InvalidState, "WindowsJITScriptingBackend failed to get hostfxr runtime delegate.");
            }

            hostContext = initializedContext;
            loadAssemblyAndGetFunctionPointer = loadedDelegate;
            return ErrorCode::None;
        }

        [[nodiscard]] ScriptFieldKind ParseScriptFieldKind(std::string_view text) noexcept
        {
            if (text == "Bool")
            {
                return ScriptFieldKind::Bool;
            }
            if (text == "Int")
            {
                return ScriptFieldKind::Int;
            }
            if (text == "Float")
            {
                return ScriptFieldKind::Float;
            }
            if (text == "String")
            {
                return ScriptFieldKind::String;
            }
            if (text == "Vector3")
            {
                return ScriptFieldKind::Vector3;
            }
            if (text == "Color")
            {
                return ScriptFieldKind::Color;
            }
            if (text == "Enum")
            {
                return ScriptFieldKind::Enum;
            }

            return ScriptFieldKind::Unsupported;
        }

        [[nodiscard]] ScriptFieldInfo ReadScriptFieldInfo(const boost::json::object& object)
        {
            ScriptFieldInfo info;
            if (const boost::json::value* name = object.if_contains("name"); name != nullptr && name->is_string())
            {
                info.name = std::string(name->as_string());
            }
            if (const boost::json::value* displayName = object.if_contains("displayName"); displayName != nullptr && displayName->is_string())
            {
                info.displayName = std::string(displayName->as_string());
            }
            else
            {
                info.displayName = info.name;
            }
            if (const boost::json::value* kind = object.if_contains("kind"); kind != nullptr && kind->is_string())
            {
                info.kind = ParseScriptFieldKind(std::string_view(kind->as_string().data(), kind->as_string().size()));
            }
            if (const boost::json::value* managedTypeName = object.if_contains("managedTypeName"); managedTypeName != nullptr && managedTypeName->is_string())
            {
                info.managedTypeName = std::string(managedTypeName->as_string());
            }
            if (const boost::json::value* enumNames = object.if_contains("enumNames"); enumNames != nullptr && enumNames->is_array())
            {
                for (const boost::json::value& enumName : enumNames->as_array())
                {
                    if (enumName.is_string())
                    {
                        info.enumNames.emplace_back(enumName.as_string());
                    }
                }
            }
            if (const boost::json::value* defaultValue = object.if_contains("defaultValue"); defaultValue != nullptr)
            {
                info.defaultValueJson = boost::json::serialize(*defaultValue);
            }

            return info;
        }

#endif
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
        const ManagedScriptBindingInitParam bindingInitParam{
            loadAssemblyAndGetFunctionPointer_,
            runtimeConfigPath_,
        };
        const ErrorCode result = LoadManagedEntryPoints(desc, bindingInitParam, entryPoints);
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
        RegisterNativeScriptApi(entryPoints_);
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
            if (const boost::json::value* fields = object.if_contains("fields"); fields != nullptr && fields->is_array())
            {
                for (const boost::json::value& fieldValue : fields->as_array())
                {
                    if (!fieldValue.is_object())
                    {
                        continue;
                    }

                    ScriptFieldInfo fieldInfo = ReadScriptFieldInfo(fieldValue.as_object());
                    if (!fieldInfo.name.empty() && fieldInfo.kind != ScriptFieldKind::Unsupported)
                    {
                        info.fields.push_back(std::move(fieldInfo));
                    }
                }
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

        ScriptInstanceHandle handle = entryPoints_.create(desc.component, desc.typeName.c_str(), desc.invokeOnCreate ? 1 : 0);
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
            if (entryPoints_.createEvent != nullptr)
            {
                entryPoints_.createEvent(script);
            }
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

    Result<std::string> WindowsJITScriptingBackend::GetScriptFieldsJson(ScriptInstanceHandle script)
    {
        if (script == 0 || entryPoints_.getFieldsJson == nullptr)
        {
            return Result<std::string>::Failure(Error(ErrorCode::InvalidState, "Script field read requires a valid managed script instance."));
        }

        const char* jsonText = entryPoints_.getFieldsJson(script);
        if (jsonText == nullptr)
        {
            return Result<std::string>::Failure(Error(ErrorCode::InvalidState, "Managed script bridge failed to read script fields."));
        }

        std::string result(jsonText);
        if (entryPoints_.freeString != nullptr)
        {
            entryPoints_.freeString(jsonText);
        }

        return Result<std::string>::Success(std::move(result));
    }

    ErrorCode WindowsJITScriptingBackend::SetScriptFieldsJson(ScriptInstanceHandle script, std::string_view fieldsJson)
    {
        if (script == 0 || entryPoints_.setFieldsJson == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        std::string fields(fieldsJson);
        return entryPoints_.setFieldsJson(script, fields.c_str()) == 0 ? ErrorCode::None : ErrorCode::InvalidArgument;
    }

    ErrorCode WindowsJITScriptingBackend::SetScriptFieldJson(ScriptInstanceHandle script, std::string_view fieldName, std::string_view valueJson)
    {
        if (script == 0 || entryPoints_.setFieldJson == nullptr)
        {
            return ErrorCode::InvalidState;
        }

        std::string name(fieldName);
        std::string value(valueJson);
        return entryPoints_.setFieldJson(script, name.c_str(), value.c_str()) == 0 ? ErrorCode::None : ErrorCode::InvalidArgument;
    }

    ErrorCode WindowsJITScriptingBackend::InitializeHost(const ScriptingSystemInitParam& initParam)
    {
#if !VE_PLATFORM_WINDOWS
        static_cast<void>(initParam);
        return ErrorCode::Unsupported;
#else
        HostRuntimePaths paths;
        ErrorCode result = ResolveHostRuntimePaths(initParam, paths);
        if (result != ErrorCode::None)
        {
            return result;
        }

        HostFxrExports exports;
        result = LoadHostFxrExports(paths.hostFxrPath, exports);
        if (result != ErrorCode::None)
        {
            return result;
        }

        HostFxrHandle hostContext = nullptr;
        void* loadAssemblyAndGetFunctionPointer = nullptr;
        result = InitializeRuntimeHost(paths, exports, hostContext, loadAssemblyAndGetFunctionPointer);
        if (result != ErrorCode::None)
        {
            FreeLibrary(exports.library);
            return result;
        }

        // Step 7: publish the initialized host state only after every required step succeeds.
        hostFxrLibrary_ = exports.library;
        hostFxrContext_ = hostContext;
        loadAssemblyAndGetFunctionPointer_ = loadAssemblyAndGetFunctionPointer;
        closeHostFxr_ = reinterpret_cast<void*>(exports.close);
        runtimeRoot_ = std::move(paths.runtimeRoot);
        runtimeConfigPath_ = std::move(paths.runtimeConfigPath);

        VE_LOG_INFO_CATEGORY("Script", "Initialized Windows JIT .NET host from {}.", paths.hostFxrPath.string());
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
} // namespace ve
