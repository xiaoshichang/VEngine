#include "Engine/Runtime/Scripting/WindowsJITScriptingBackend.h"

#include "Engine/Runtime/Core/Platform.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Logging/Log.h"

#include <algorithm>
#include <array>
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
    } // namespace

    WindowsJITScriptingBackend::~WindowsJITScriptingBackend()
    {
        Shutdown();
    }

    ScriptingBackendType WindowsJITScriptingBackend::GetBackendType() const noexcept
    {
        return ScriptingBackendType::WindowsJIT;
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
            return ErrorCode::NotFound;
        }

        std::filesystem::path runtimeConfigPath;
        if (!initParam.runtimeConfigPath.IsEmpty())
        {
            runtimeConfigPath = ToNativePath(initParam.runtimeConfigPath);
            if (!std::filesystem::exists(runtimeConfigPath, error))
            {
                return ErrorCode::NotFound;
            }
        }

        const std::filesystem::path hostFxrPath = ResolveHostFxrPath(runtimeRoot);
        if (hostFxrPath.empty())
        {
            return ErrorCode::NotFound;
        }

        HMODULE hostFxrLibrary = LoadLibraryW(hostFxrPath.c_str());
        if (hostFxrLibrary == nullptr)
        {
            return ErrorCode::PlatformError;
        }

        auto initializeForRuntimeConfig =
            ResolveHostFxrExport<HostFxrInitializeForRuntimeConfigFn>(hostFxrLibrary, "hostfxr_initialize_for_runtime_config");
        auto getRuntimeDelegate = ResolveHostFxrExport<HostFxrGetRuntimeDelegateFn>(hostFxrLibrary, "hostfxr_get_runtime_delegate");
        auto closeHostFxr = ResolveHostFxrExport<HostFxrCloseFn>(hostFxrLibrary, "hostfxr_close");
        if (initializeForRuntimeConfig == nullptr || getRuntimeDelegate == nullptr || closeHostFxr == nullptr)
        {
            FreeLibrary(hostFxrLibrary);
            return ErrorCode::InvalidState;
        }

        HostFxrHandle hostContext = nullptr;
        void* loadAssemblyAndGetFunctionPointer = nullptr;
        if (!runtimeConfigPath.empty())
        {
            const std::wstring runtimeRootText = runtimeRoot.wstring();
            const HostFxrInitializeParameters parameters{
                sizeof(HostFxrInitializeParameters),
                nullptr,
                runtimeRootText.c_str(),
            };

            int result = initializeForRuntimeConfig(runtimeConfigPath.c_str(), &parameters, &hostContext);
            if (result != 0 || hostContext == nullptr)
            {
                FreeLibrary(hostFxrLibrary);
                return ErrorCode::InvalidState;
            }

            result = getRuntimeDelegate(hostContext, HostFxrDelegateType::LoadAssemblyAndGetFunctionPointer, &loadAssemblyAndGetFunctionPointer);
            if (result != 0 || loadAssemblyAndGetFunctionPointer == nullptr)
            {
                closeHostFxr(hostContext);
                FreeLibrary(hostFxrLibrary);
                return ErrorCode::InvalidState;
            }
        }

        hostFxrLibrary_ = hostFxrLibrary;
        hostFxrContext_ = hostContext;
        loadAssemblyAndGetFunctionPointer_ = loadAssemblyAndGetFunctionPointer;
        closeHostFxr_ = reinterpret_cast<void*>(closeHostFxr);
        SetResolvedRuntimePaths(std::move(runtimeRoot), std::move(runtimeConfigPath));

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
        if (loadAssemblyAndGetFunctionPointer_ == nullptr || GetRuntimeConfigPath().empty())
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

        ErrorCode result = LoadEntryPoint(loadFunction, assemblyPath.wstring(), bridgeTypeName, L"CreateScript", loadedEntryPoints.create);
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
