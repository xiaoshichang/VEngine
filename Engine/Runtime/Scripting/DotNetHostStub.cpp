#include "Engine/Runtime/Scripting/DotNetHost.h"

namespace ve
{
    struct DotNetHost::Impl
    {
        bool initialized = false;
    };

    DotNetHost::DotNetHost()
        : impl_(std::make_unique<Impl>())
    {
    }

    DotNetHost::~DotNetHost() = default;

    Result<DotNetHostInfo> DotNetHost::Initialize(const DotNetHostDesc&)
    {
        return Result<DotNetHostInfo>::Failure(
            Error(ErrorCode::Unsupported, "DotNetHost is currently implemented only on Windows."));
    }

    void DotNetHost::Shutdown() noexcept
    {
        impl_->initialized = false;
    }

    bool DotNetHost::IsInitialized() const noexcept
    {
        return impl_->initialized;
    }

    Result<void*> DotNetHost::LoadAssemblyFunction(const DotNetAssemblyFunctionDesc&)
    {
        return Result<void*>::Failure(
            Error(ErrorCode::Unsupported, "DotNetHost is currently implemented only on Windows."));
    }
} // namespace ve
