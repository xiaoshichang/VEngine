#include "Engine/Runtime/Application/EngineRuntime.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"

#include <exception>

namespace ve
{
    namespace
    {
        [[noreturn]] void TerminateRuntimeInitialization(const char* systemName, ErrorCode errorCode)
        {
            VE_LOG_FATAL("{} initialization failed: {}", systemName, ToString(errorCode));
            std::terminate();
        }
    } // namespace

    EngineRuntime::EngineRuntime() = default;

    EngineRuntime::~EngineRuntime()
    {
        Shutdown();
    }

    ErrorCode EngineRuntime::Initialize(const EngineRuntimeDesc& desc)
    {
        if (state_ == EngineRuntimeState::Initialized)
        {
            return ErrorCode::InvalidState;
        }

        if (state_ == EngineRuntimeState::Shutdown)
        {
            return ErrorCode::InvalidState;
        }

        ErrorCode jobSystemResult = jobSystem_.Initialize(desc.jobSystem);
        if (jobSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("JobSystem", jobSystemResult);
        }

        ErrorCode ioSystemResult = ioSystem_.Initialize(desc.ioSystem);
        if (ioSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("IOSystem", ioSystemResult);
        }

        renderSystem_.Initialize(desc.renderSystem);

        ErrorCode gameThreadSystemResult = gameThreadSystem_.Initialize(desc.gameThreadSystem);
        if (gameThreadSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("GameThreadSystem", gameThreadSystemResult);
        }

        state_ = EngineRuntimeState::Initialized;
        VE_LOG_INFO("JobSystem initialized with {} worker thread(s).", jobSystem_.GetWorkerThreadCount());
        VE_LOG_INFO("IOSystem initialized.");
        VE_LOG_INFO("RenderSystem initialized.");
        VE_LOG_INFO("GameThreadSystem initialized.");
        return ErrorCode::None;
    }

    void EngineRuntime::Shutdown() noexcept
    {
        if (state_ != EngineRuntimeState::Initialized)
        {
            return;
        }

        gameThreadSystem_.Shutdown();
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

    GameThreadSystem& EngineRuntime::GetGameThreadSystem() noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetGameThreadSystem requires an initialized runtime.");
        return gameThreadSystem_;
    }

    const GameThreadSystem& EngineRuntime::GetGameThreadSystem() const noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetGameThreadSystem requires an initialized runtime.");
        return gameThreadSystem_;
    }
} // namespace ve
