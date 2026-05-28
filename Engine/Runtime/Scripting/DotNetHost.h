#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/FileSystem/Path.h"

#include <cstdint>
#include <memory>
#include <string>

namespace ve
{
#if defined(_WIN32)
#define VE_DOTNET_CALLTYPE __stdcall
#else
#define VE_DOTNET_CALLTYPE
#endif

    struct DotNetHostDesc
    {
        Path runtimeConfigPath;
    };

    struct DotNetHostInfo
    {
        Path hostFxrPath;
    };

    struct DotNetAssemblyFunctionDesc
    {
        Path assemblyPath;
        std::string typeName;
        std::string methodName;
        std::string delegateTypeName;
        bool unmanagedCallersOnly = false;
    };

    using DotNetComponentEntryPoint = int(VE_DOTNET_CALLTYPE*)(void* argument, std::int32_t argumentSizeInBytes);

    class DotNetHost : public NonMovable
    {
    public:
        DotNetHost();
        ~DotNetHost();

        [[nodiscard]] Result<DotNetHostInfo> Initialize(const DotNetHostDesc& desc);
        void Shutdown() noexcept;

        [[nodiscard]] bool IsInitialized() const noexcept;
        [[nodiscard]] Result<void*> LoadAssemblyFunction(const DotNetAssemblyFunctionDesc& desc);

    private:
        struct Impl;

        std::unique_ptr<Impl> impl_;
    };

#undef VE_DOTNET_CALLTYPE
} // namespace ve
