#include "Engine/Runtime/Application/EngineRuntime.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

#include <exception>

namespace ve
{
namespace
{
[[noreturn]] void TerminateRuntimeInitialization(const char* systemName, const Error& error)
{
    VE_LOG_FATAL(
        "{} initialization failed: {}{}{}",
        systemName,
        ToString(error.GetCode()),
        error.GetMessage().empty() ? "" : ": ",
        error.GetMessage());
    std::terminate();
}
}

EngineRuntime::EngineRuntime() = default;

EngineRuntime::~EngineRuntime()
{
    Shutdown();
}

Result<void> EngineRuntime::Initialize(const EngineRuntimeDesc& desc)
{
    if (state_ == EngineRuntimeState::Initialized)
    {
        return Result<void>::Failure(Error(ErrorCode::InvalidState, "EngineRuntime is already initialized."));
    }

    if (state_ == EngineRuntimeState::Shutdown)
    {
        return Result<void>::Failure(
            Error(ErrorCode::InvalidState, "EngineRuntime does not support repeated lifecycles."));
    }

    Result<void> jobSystemResult = jobSystem_.Initialize(desc.jobSystem);
    if (!jobSystemResult)
    {
        TerminateRuntimeInitialization("JobSystem", jobSystemResult.GetError());
    }

    Result<void> ioSystemResult = ioSystem_.Initialize(desc.ioSystem);
    if (!ioSystemResult)
    {
        TerminateRuntimeInitialization("IOSystem", ioSystemResult.GetError());
    }

    Result<void> renderSystemResult = renderSystem_.Initialize(desc.renderSystem);
    if (!renderSystemResult)
    {
        TerminateRuntimeInitialization("RenderSystem", renderSystemResult.GetError());
    }

    state_ = EngineRuntimeState::Initialized;
    VE_LOG_INFO("JobSystem initialized with {} worker thread(s).", jobSystem_.GetWorkerThreadCount());
    VE_LOG_INFO("IOSystem initialized.");
    VE_LOG_INFO("RenderSystem initialized.");
    return Result<void>::Success();
}

void EngineRuntime::Shutdown() noexcept
{
    if (state_ != EngineRuntimeState::Initialized)
    {
        return;
    }

    renderSystem_.Shutdown();
    ioSystem_.Shutdown();
    jobSystem_.Shutdown();
    state_ = EngineRuntimeState::Shutdown;
}

bool EngineRuntime::IsInitialized() const noexcept
{
    return state_ == EngineRuntimeState::Initialized;
}

bool EngineRuntime::HasInitialized() const noexcept
{
    return state_ != EngineRuntimeState::NotInitialized;
}

JobSystem& EngineRuntime::GetJobSystem() noexcept
{
    VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetJobSystem requires an initialized runtime.");
    return jobSystem_;
}

const JobSystem& EngineRuntime::GetJobSystem() const noexcept
{
    VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetJobSystem requires an initialized runtime.");
    return jobSystem_;
}

IOSystem& EngineRuntime::GetIOSystem() noexcept
{
    VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetIOSystem requires an initialized runtime.");
    return ioSystem_;
}

const IOSystem& EngineRuntime::GetIOSystem() const noexcept
{
    VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetIOSystem requires an initialized runtime.");
    return ioSystem_;
}

RenderSystem& EngineRuntime::GetRenderSystem() noexcept
{
    VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetRenderSystem requires an initialized runtime.");
    return renderSystem_;
}

const RenderSystem& EngineRuntime::GetRenderSystem() const noexcept
{
    VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetRenderSystem requires an initialized runtime.");
    return renderSystem_;
}
}
