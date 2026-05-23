#include "Engine/Runtime/Application/EngineRuntime.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

namespace ve
{
EngineRuntime::EngineRuntime() = default;

EngineRuntime::~EngineRuntime()
{
    Shutdown();
}

Result<void> EngineRuntime::Initialize(const EngineRuntimeDesc& desc)
{
    if (initialized_)
    {
        return Result<void>::Failure(Error(ErrorCode::InvalidState, "EngineRuntime is already initialized."));
    }

    if (hasInitialized_)
    {
        return Result<void>::Failure(
            Error(ErrorCode::InvalidState, "EngineRuntime does not support repeated lifecycles."));
    }

    Result<void> jobSystemResult = jobSystem_.Initialize(desc.jobSystem);
    if (!jobSystemResult)
    {
        return jobSystemResult;
    }

    Result<void> ioSystemResult = ioSystem_.Initialize(desc.ioSystem);
    if (!ioSystemResult)
    {
        jobSystem_.Shutdown();
        return ioSystemResult;
    }

    initialized_ = true;
    hasInitialized_ = true;
    VE_LOG_INFO("JobSystem initialized with {} worker thread(s).", jobSystem_.GetWorkerThreadCount());
    VE_LOG_INFO("IOSystem initialized.");
    return Result<void>::Success();
}

void EngineRuntime::Shutdown() noexcept
{
    if (!initialized_)
    {
        return;
    }

    ioSystem_.Shutdown();
    jobSystem_.Shutdown();
    initialized_ = false;
}

bool EngineRuntime::IsInitialized() const noexcept
{
    return initialized_;
}

bool EngineRuntime::HasInitialized() const noexcept
{
    return hasInitialized_;
}

JobSystem& EngineRuntime::GetJobSystem() noexcept
{
    VE_ASSERT_MESSAGE(initialized_, "EngineRuntime::GetJobSystem requires an initialized runtime.");
    return jobSystem_;
}

const JobSystem& EngineRuntime::GetJobSystem() const noexcept
{
    VE_ASSERT_MESSAGE(initialized_, "EngineRuntime::GetJobSystem requires an initialized runtime.");
    return jobSystem_;
}

IOSystem& EngineRuntime::GetIOSystem() noexcept
{
    VE_ASSERT_MESSAGE(initialized_, "EngineRuntime::GetIOSystem requires an initialized runtime.");
    return ioSystem_;
}

const IOSystem& EngineRuntime::GetIOSystem() const noexcept
{
    VE_ASSERT_MESSAGE(initialized_, "EngineRuntime::GetIOSystem requires an initialized runtime.");
    return ioSystem_;
}
}
