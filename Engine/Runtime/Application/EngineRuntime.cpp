#include "Engine/Runtime/Application/EngineRuntime.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
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

    ErrorCode EngineRuntime::Initialize(const EngineRuntimeInitParam& desc)
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
        VE_LOG_INFO("JobSystem initialized with {} worker thread(s).", jobSystem_.GetWorkerThreadCount());

        ErrorCode ioSystemResult = ioSystem_.Initialize(desc.ioSystem);
        if (ioSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("IOSystem", ioSystemResult);
        }
        VE_LOG_INFO("IOSystem initialized.");

        ErrorCode inputSystemResult = inputSystem_.Initialize(desc.inputSystem);
        if (inputSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("InputSystem", inputSystemResult);
        }
        VE_LOG_INFO("InputSystem initialized.");

        mainThreadSceneThreadFrameEndSync_.Reset();
        sceneThreadRenderThreadFrameEndSync_.Reset();
        renderSystem_.SetSceneThreadRenderThreadFrameEndSync(&sceneThreadRenderThreadFrameEndSync_);
        sceneSystem_.SetMainThreadSceneThreadFrameEndSync(&mainThreadSceneThreadFrameEndSync_);
        sceneSystem_.SetSceneThreadRenderThreadFrameEndSync(&sceneThreadRenderThreadFrameEndSync_);

        ErrorCode renderSystemResult = renderSystem_.Initialize(desc.renderSystem);
        if (renderSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("RenderSystem", renderSystemResult);
        }
        VE_LOG_INFO("RenderSystem initialized.");

        ErrorCode timeSystemResult = timeSystem_.Initialize(desc.timeSystem);
        if (timeSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("TimeSystem", timeSystemResult);
        }
        VE_LOG_INFO("TimeSystem initialized.");

        ErrorCode physicsSystemResult = physicsSystem_.Initialize(desc.physicsSystem, jobSystem_);
        if (physicsSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("PhysicsSystem", physicsSystemResult);
        }
        VE_LOG_INFO("PhysicsSystem initialized.");

        ErrorCode sceneSystemResult = sceneSystem_.Initialize(desc.sceneSystem, timeSystem_, inputSystem_, renderSystem_, physicsSystem_);
        if (sceneSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("SceneSystem", sceneSystemResult);
        }
        VE_LOG_INFO("SceneSystem initialized.");

        const Path projectRoot = FileSystem::GetProjectRoot();
        ErrorCode resourceSystemResult = resourceSystem_.Initialize(ResourceSystemInitParam{projectRoot});
        if (resourceSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("ResourceSystem", resourceSystemResult);
        }
        VE_LOG_INFO("ResourceSystem initialized.");

        ErrorCode scriptingSystemResult = scriptingSystem_.Initialize(desc.scriptingSystem);
        if (scriptingSystemResult != ErrorCode::None)
        {
            TerminateRuntimeInitialization("ScriptingSystem", scriptingSystemResult);
        }
        VE_LOG_INFO("ScriptingSystem initialized.");

        state_ = EngineRuntimeState::Initialized;
        VE_LOG_INFO("all systems initialized.");
        return ErrorCode::None;
    }

    void EngineRuntime::Shutdown() noexcept
    {
        if (state_ != EngineRuntimeState::Initialized)
        {
            return;
        }

        sceneSystem_.Shutdown();
        physicsSystem_.Shutdown();
        scriptingSystem_.Shutdown();
        resourceSystem_.Shutdown();
        timeSystem_.Shutdown();
        renderSystem_.Shutdown();
        inputSystem_.Shutdown();
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

    InputSystem& EngineRuntime::GetInputSystem() noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetInputSystem requires an initialized runtime.");
        return inputSystem_;
    }

    const InputSystem& EngineRuntime::GetInputSystem() const noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetInputSystem requires an initialized runtime.");
        return inputSystem_;
    }

    TimeSystem& EngineRuntime::GetTimeSystem() noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetTimeSystem requires an initialized runtime.");
        return timeSystem_;
    }

    const TimeSystem& EngineRuntime::GetTimeSystem() const noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetTimeSystem requires an initialized runtime.");
        return timeSystem_;
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

    SceneSystem& EngineRuntime::GetSceneSystem() noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetSceneSystem requires an initialized runtime.");
        return sceneSystem_;
    }

    const SceneSystem& EngineRuntime::GetSceneSystem() const noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetSceneSystem requires an initialized runtime.");
        return sceneSystem_;
    }

    ResourceSystem& EngineRuntime::GetResourceSystem() noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetResourceSystem requires an initialized runtime.");
        return resourceSystem_;
    }

    const ResourceSystem& EngineRuntime::GetResourceSystem() const noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetResourceSystem requires an initialized runtime.");
        return resourceSystem_;
    }

    ScriptingSystem& EngineRuntime::GetScriptingSystem() noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetScriptingSystem requires an initialized runtime.");
        return scriptingSystem_;
    }

    const ScriptingSystem& EngineRuntime::GetScriptingSystem() const noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetScriptingSystem requires an initialized runtime.");
        return scriptingSystem_;
    }

    PhysicsSystem& EngineRuntime::GetPhysicsSystem() noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetPhysicsSystem requires an initialized runtime.");
        return physicsSystem_;
    }

    const PhysicsSystem& EngineRuntime::GetPhysicsSystem() const noexcept
    {
        VE_ASSERT_MESSAGE(IsInitialized(), "EngineRuntime::GetPhysicsSystem requires an initialized runtime.");
        return physicsSystem_;
    }
} // namespace ve
