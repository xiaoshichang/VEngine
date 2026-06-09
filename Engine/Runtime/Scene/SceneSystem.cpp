#include "Engine/Runtime/Scene/SceneSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"
#include "Engine/Runtime/Threading/Atomic.h"

#include <exception>
#include <new>

namespace ve
{
    struct SceneSystemImpl
    {
        Thread thread;
        std::unique_ptr<Scene> scene;
        TimeSystem* timeSystem = nullptr;
        Atomic<UInt64> sceneThreadIdValue{0};
        AtomicBool initialized{false};
        AtomicBool stopRequested{false};
    };

    namespace
    {
        void UpdateScene(SceneSystemImpl& impl, Float32 deltaSeconds)
        {
            VE_ASSERT_SCENE_THREAD();
            if (impl.scene != nullptr)
            {
                impl.scene->Update(deltaSeconds);
            }
        }

        void SceneThreadLoop(SceneSystemImpl& impl)
        {
            const ThreadId sceneThreadId = GetCurrentThreadId();
            impl.sceneThreadIdValue.store(sceneThreadId.value, std::memory_order_release);
            SetExpectedSceneThreadId(sceneThreadId);

            VE_ASSERT_MESSAGE(impl.timeSystem != nullptr, "impl.timeSystem should not be nullptr");
            VE_ASSERT_MESSAGE(impl.timeSystem->IsInitialized(), "impl.timeSystem should be initialized.");

            while (!impl.stopRequested.load(std::memory_order_acquire))
            {
                impl.timeSystem->Tick();
                const TimeSnapshot timeSnapshot = impl.timeSystem->GetSnapshot();

                try
                {
                    UpdateScene(impl, timeSnapshot.deltaSeconds);
                }
                catch (...)
                {
                    VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped SceneSystem update.");
                }
            }

            impl.sceneThreadIdValue.store(0, std::memory_order_release);
            SetExpectedSceneThreadId(ThreadId{});
        }

        void StopAndJoinSceneThread(SceneSystemImpl& impl) noexcept
        {
            impl.stopRequested.store(true, std::memory_order_release);

            if (impl.thread.IsJoinable())
            {
                const bool joined = impl.thread.Join();
                VE_ASSERT_MESSAGE(joined, "SceneSystem failed to join its Scene Thread during shutdown.");
            }

            impl.initialized.store(false, std::memory_order_release);
            impl.stopRequested.store(false, std::memory_order_release);
            impl.timeSystem = nullptr;
        }
    } // namespace

    SceneSystem::SceneSystem()
        : impl_(std::make_unique<SceneSystemImpl>())
    {
    }

    SceneSystem::~SceneSystem()
    {
        Shutdown();
    }

    ErrorCode SceneSystem::Initialize(const SceneSystemInitParam& initParam, TimeSystem& timeSystem)
    {
        if (impl_->initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        if (!timeSystem.IsInitialized())
        {
            return ErrorCode::InvalidState;
        }

        impl_->timeSystem = &timeSystem;
        impl_->stopRequested.store(false, std::memory_order_release);

        ErrorCode startResult = impl_->thread.Start(ThreadDesc{initParam.threadName},
                                                    [this]() { SceneThreadLoop(*impl_); });
        if (startResult != ErrorCode::None)
        {
            throw;
        }

        impl_->initialized.store(true, std::memory_order_release);
        return ErrorCode::None;
    }

    void SceneSystem::Shutdown() noexcept
    {
        if (!impl_->initialized.load(std::memory_order_acquire))
        {
            return;
        }

        StopAndJoinSceneThread(*impl_);
    }

    bool SceneSystem::IsInitialized() const noexcept
    {
        return impl_->initialized.load(std::memory_order_acquire);
    }

    ThreadId SceneSystem::GetSceneThreadId() const noexcept
    {
        return ThreadId{impl_->sceneThreadIdValue.load(std::memory_order_acquire)};
    }

    Scene* SceneSystem::GetScene() noexcept
    {
        return impl_->scene.get();
    }

    const Scene* SceneSystem::GetScene() const noexcept
    {
        return impl_->scene.get();
    }

    void SceneSystem::ReplaceScene(std::unique_ptr<Scene> scene)
    {
        impl_->scene = scene != nullptr ? std::move(scene) : std::make_unique<Scene>();
    }

    void SceneSystem::Update(Float32 deltaSeconds)
    {
        UpdateScene(*impl_, deltaSeconds);
    }
} // namespace ve
