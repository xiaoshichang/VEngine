#include "Engine/Runtime/Scripting/DotNetHost.h"

#include "Engine/Runtime/FileSystem/FileSystem.h"

#include <coreclr_delegates.h>
#include <hostfxr.h>
#include <nethost.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

#include <cstddef>
#include <sstream>
#include <string_view>

namespace ve
{
    namespace
    {
        [[nodiscard]] std::string MakeStatusMessage(std::string_view action, int status)
        {
            std::ostringstream stream;
            stream << action << " failed with status 0x" << std::hex << status << ".";
            return stream.str();
        }

        [[nodiscard]] std::string MakeWin32Message(std::string_view action)
        {
            std::ostringstream stream;
            stream << action << " failed with Win32 error " << GetLastError() << ".";
            return stream.str();
        }

        [[nodiscard]] Result<std::wstring> ToWideString(std::string_view text, std::string_view label)
        {
            if (text.empty())
            {
                return Result<std::wstring>::Success(std::wstring());
            }

            const int requiredSize = MultiByteToWideChar(CP_UTF8,
                                                         MB_ERR_INVALID_CHARS,
                                                         text.data(),
                                                         static_cast<int>(text.size()),
                                                         nullptr,
                                                         0);
            if (requiredSize <= 0)
            {
                return Result<std::wstring>::Failure(
                    Error(ErrorCode::InvalidArgument, std::string(label) + " is not valid UTF-8."));
            }

            std::wstring result(static_cast<std::size_t>(requiredSize), L'\0');
            const int convertedSize = MultiByteToWideChar(CP_UTF8,
                                                          MB_ERR_INVALID_CHARS,
                                                          text.data(),
                                                          static_cast<int>(text.size()),
                                                          result.data(),
                                                          requiredSize);
            if (convertedSize != requiredSize)
            {
                return Result<std::wstring>::Failure(
                    Error(ErrorCode::PlatformError, "UTF-8 to UTF-16 conversion produced an unexpected length."));
            }

            return Result<std::wstring>::Success(std::move(result));
        }

        [[nodiscard]] Result<std::string> ToUtf8String(std::wstring_view text, std::string_view label)
        {
            if (text.empty())
            {
                return Result<std::string>::Success(std::string());
            }

            const int requiredSize = WideCharToMultiByte(CP_UTF8,
                                                         WC_ERR_INVALID_CHARS,
                                                         text.data(),
                                                         static_cast<int>(text.size()),
                                                         nullptr,
                                                         0,
                                                         nullptr,
                                                         nullptr);
            if (requiredSize <= 0)
            {
                return Result<std::string>::Failure(
                    Error(ErrorCode::InvalidArgument, std::string(label) + " is not valid UTF-16."));
            }

            std::string result(static_cast<std::size_t>(requiredSize), '\0');
            const int convertedSize = WideCharToMultiByte(CP_UTF8,
                                                          WC_ERR_INVALID_CHARS,
                                                          text.data(),
                                                          static_cast<int>(text.size()),
                                                          result.data(),
                                                          requiredSize,
                                                          nullptr,
                                                          nullptr);
            if (convertedSize != requiredSize)
            {
                return Result<std::string>::Failure(
                    Error(ErrorCode::PlatformError, "UTF-16 to UTF-8 conversion produced an unexpected length."));
            }

            return Result<std::string>::Success(std::move(result));
        }

        [[nodiscard]] Result<std::wstring> ResolveHostFxrPath()
        {
            size_t bufferSize = 0;
            int result = get_hostfxr_path(nullptr, &bufferSize, nullptr);
            if (bufferSize == 0)
            {
                return Result<std::wstring>::Failure(
                    Error(ErrorCode::NotFound, MakeStatusMessage("get_hostfxr_path size query", result)));
            }

            std::wstring path(bufferSize, L'\0');
            result = get_hostfxr_path(path.data(), &bufferSize, nullptr);
            if (result != 0)
            {
                return Result<std::wstring>::Failure(
                    Error(ErrorCode::NotFound, MakeStatusMessage("get_hostfxr_path", result)));
            }

            const std::size_t terminator = path.find(L'\0');
            if (terminator != std::wstring::npos)
            {
                path.resize(terminator);
            }

            if (path.empty())
            {
                return Result<std::wstring>::Failure(Error(ErrorCode::NotFound, "hostfxr path resolved to empty."));
            }

            return Result<std::wstring>::Success(std::move(path));
        }

        template<typename TFunction>
        [[nodiscard]] Result<TFunction> LoadHostFxrExport(HMODULE module, const char* name)
        {
            FARPROC proc = GetProcAddress(module, name);
            if (proc == nullptr)
            {
                return Result<TFunction>::Failure(Error(ErrorCode::PlatformError, MakeWin32Message(name)));
            }

            return Result<TFunction>::Success(reinterpret_cast<TFunction>(proc));
        }
    } // namespace

    struct DotNetHost::Impl
    {
        ~Impl()
        {
            if (hostFxrModule != nullptr)
            {
                FreeLibrary(hostFxrModule);
                hostFxrModule = nullptr;
            }
        }

        HMODULE hostFxrModule = nullptr;
        hostfxr_initialize_for_runtime_config_fn initializeForRuntimeConfig = nullptr;
        hostfxr_get_runtime_delegate_fn getRuntimeDelegate = nullptr;
        hostfxr_close_fn close = nullptr;
        load_assembly_and_get_function_pointer_fn loadAssemblyAndGetFunctionPointer = nullptr;
        Path hostFxrPath;
        bool initialized = false;
    };

    DotNetHost::DotNetHost()
        : impl_(std::make_unique<Impl>())
    {
    }

    DotNetHost::~DotNetHost() = default;

    Result<DotNetHostInfo> DotNetHost::Initialize(const DotNetHostDesc& desc)
    {
        if (impl_->initialized)
        {
            return Result<DotNetHostInfo>::Failure(
                Error(ErrorCode::InvalidState, "DotNetHost is already initialized."));
        }

        if (desc.runtimeConfigPath.IsEmpty())
        {
            return Result<DotNetHostInfo>::Failure(
                Error(ErrorCode::InvalidArgument, "DotNetHost requires a runtimeconfig.json path."));
        }

        if (!FileSystem::IsFile(desc.runtimeConfigPath))
        {
            return Result<DotNetHostInfo>::Failure(
                Error(ErrorCode::NotFound,
                      "DotNetHost runtimeconfig.json was not found: " + desc.runtimeConfigPath.GetString()));
        }

        Result<std::wstring> runtimeConfigPath = ToWideString(desc.runtimeConfigPath.GetString(), "runtimeconfig path");
        if (!runtimeConfigPath)
        {
            return Result<DotNetHostInfo>::Failure(runtimeConfigPath.GetError());
        }

        Result<std::wstring> hostFxrPath = ResolveHostFxrPath();
        if (!hostFxrPath)
        {
            return Result<DotNetHostInfo>::Failure(hostFxrPath.GetError());
        }

        impl_->hostFxrModule = LoadLibraryW(hostFxrPath.GetValue().c_str());
        if (impl_->hostFxrModule == nullptr)
        {
            return Result<DotNetHostInfo>::Failure(Error(ErrorCode::PlatformError, MakeWin32Message("LoadLibraryW")));
        }

        Result<hostfxr_initialize_for_runtime_config_fn> initializeForRuntimeConfig =
            LoadHostFxrExport<hostfxr_initialize_for_runtime_config_fn>(impl_->hostFxrModule,
                                                                        "hostfxr_initialize_for_runtime_config");
        if (!initializeForRuntimeConfig)
        {
            return Result<DotNetHostInfo>::Failure(initializeForRuntimeConfig.GetError());
        }

        Result<hostfxr_get_runtime_delegate_fn> getRuntimeDelegate =
            LoadHostFxrExport<hostfxr_get_runtime_delegate_fn>(impl_->hostFxrModule, "hostfxr_get_runtime_delegate");
        if (!getRuntimeDelegate)
        {
            return Result<DotNetHostInfo>::Failure(getRuntimeDelegate.GetError());
        }

        Result<hostfxr_close_fn> close = LoadHostFxrExport<hostfxr_close_fn>(impl_->hostFxrModule, "hostfxr_close");
        if (!close)
        {
            return Result<DotNetHostInfo>::Failure(close.GetError());
        }

        impl_->initializeForRuntimeConfig = initializeForRuntimeConfig.GetValue();
        impl_->getRuntimeDelegate = getRuntimeDelegate.GetValue();
        impl_->close = close.GetValue();

        hostfxr_handle hostContext = nullptr;
        int result = impl_->initializeForRuntimeConfig(runtimeConfigPath.GetValue().c_str(), nullptr, &hostContext);
        if (result != 0 || hostContext == nullptr)
        {
            return Result<DotNetHostInfo>::Failure(
                Error(ErrorCode::PlatformError, MakeStatusMessage("hostfxr_initialize_for_runtime_config", result)));
        }

        void* loadAssemblyDelegate = nullptr;
        result = impl_->getRuntimeDelegate(hostContext,
                                           hdt_load_assembly_and_get_function_pointer,
                                           &loadAssemblyDelegate);
        impl_->close(hostContext);

        if (result != 0 || loadAssemblyDelegate == nullptr)
        {
            return Result<DotNetHostInfo>::Failure(
                Error(ErrorCode::PlatformError, MakeStatusMessage("hostfxr_get_runtime_delegate", result)));
        }

        impl_->loadAssemblyAndGetFunctionPointer =
            reinterpret_cast<load_assembly_and_get_function_pointer_fn>(loadAssemblyDelegate);

        Result<std::string> hostFxrPathUtf8 = ToUtf8String(hostFxrPath.GetValue(), "hostfxr path");
        if (!hostFxrPathUtf8)
        {
            return Result<DotNetHostInfo>::Failure(hostFxrPathUtf8.GetError());
        }

        impl_->hostFxrPath = Path(hostFxrPathUtf8.MoveValue());
        impl_->initialized = true;

        DotNetHostInfo info;
        info.hostFxrPath = impl_->hostFxrPath;
        return Result<DotNetHostInfo>::Success(std::move(info));
    }

    void DotNetHost::Shutdown() noexcept
    {
        impl_->loadAssemblyAndGetFunctionPointer = nullptr;
        impl_->initialized = false;
    }

    bool DotNetHost::IsInitialized() const noexcept
    {
        return impl_->initialized;
    }

    Result<void*> DotNetHost::LoadAssemblyFunction(const DotNetAssemblyFunctionDesc& desc)
    {
        if (!impl_->initialized || impl_->loadAssemblyAndGetFunctionPointer == nullptr)
        {
            return Result<void*>::Failure(Error(ErrorCode::InvalidState, "DotNetHost is not initialized."));
        }

        if (desc.assemblyPath.IsEmpty() || desc.typeName.empty() || desc.methodName.empty())
        {
            return Result<void*>::Failure(
                Error(ErrorCode::InvalidArgument, "DotNetHost requires assemblyPath, typeName, and methodName."));
        }

        if (!FileSystem::IsFile(desc.assemblyPath))
        {
            return Result<void*>::Failure(
                Error(ErrorCode::NotFound, "Managed assembly was not found: " + desc.assemblyPath.GetString()));
        }

        Result<std::wstring> assemblyPath = ToWideString(desc.assemblyPath.GetString(), "assembly path");
        if (!assemblyPath)
        {
            return Result<void*>::Failure(assemblyPath.GetError());
        }

        Result<std::wstring> typeName = ToWideString(desc.typeName, "managed type name");
        if (!typeName)
        {
            return Result<void*>::Failure(typeName.GetError());
        }

        Result<std::wstring> methodName = ToWideString(desc.methodName, "managed method name");
        if (!methodName)
        {
            return Result<void*>::Failure(methodName.GetError());
        }

        std::wstring delegateTypeNameStorage;
        const char_t* delegateTypeName = nullptr;
        if (desc.unmanagedCallersOnly)
        {
            delegateTypeName = UNMANAGEDCALLERSONLY_METHOD;
        }
        else if (!desc.delegateTypeName.empty())
        {
            Result<std::wstring> delegateTypeNameResult =
                ToWideString(desc.delegateTypeName, "managed delegate type name");
            if (!delegateTypeNameResult)
            {
                return Result<void*>::Failure(delegateTypeNameResult.GetError());
            }

            delegateTypeNameStorage = delegateTypeNameResult.MoveValue();
            delegateTypeName = delegateTypeNameStorage.c_str();
        }

        void* functionPointer = nullptr;
        const int result = impl_->loadAssemblyAndGetFunctionPointer(assemblyPath.GetValue().c_str(),
                                                                    typeName.GetValue().c_str(),
                                                                    methodName.GetValue().c_str(),
                                                                    delegateTypeName,
                                                                    nullptr,
                                                                    &functionPointer);
        if (result != 0 || functionPointer == nullptr)
        {
            return Result<void*>::Failure(
                Error(ErrorCode::PlatformError, MakeStatusMessage("load_assembly_and_get_function_pointer", result)));
        }

        return Result<void*>::Success(functionPointer);
    }
} // namespace ve
