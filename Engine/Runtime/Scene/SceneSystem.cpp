#include "Engine/Runtime/Scene/SceneSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/ThreadEnsure.h"

#include <exception>
#include <new>
#include <utility>

namespace ve
{
    struct SceneSystemImpl
    {
        Thread thread;
        Atomic<UInt64> sceneThreadIdValue{0};
        std::unique_ptr<Scene> scene;
        OSEventQueue osEventQueue;
        TimeSystem* timeSystem = nullptr;
        RenderSystem* renderSystem = nullptr;

        // frame sync between main thread and render thread.
        MainThreadSceneThreadFrameEndSync* mainThreadSceneThreadFrameEndSync = nullptr;
        SceneThreadRenderThreadFrameEndSync* sceneThreadRenderThreadFrameEndSync = nullptr;

        AtomicBool initialized{false};
        AtomicBool stopRequested{false};
    };

    namespace
    {
        void ProcessOSEvents(SceneSystemImpl& impl)
        {
            OSEvent event;
            while (impl.osEventQueue.TryPop(event))
            {
                switch (event.type)
                {
                case OSEventType::WindowFocusGained:
                case OSEventType::WindowFocusLost:
                case OSEventType::WindowMinimized:
                case OSEventType::WindowRestored:
                case OSEventType::WindowResized:
                case OSEventType::WindowShown:
                case OSEventType::WindowHidden:
                    break;
                case OSEventType::FrameEndFenceSignal:
                    if (impl.mainThreadSceneThreadFrameEndSync != nullptr)
                    {
                        impl.mainThreadSceneThreadFrameEndSync->NotifySceneThreadFrameEnd(event.fenceIndex);
                    }
                    break;
                }
            }
        }

        void UpdateScene(SceneSystemImpl& impl, Float32 deltaSeconds)
        {
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
                try
                {
                    VE_ASSERT_SCENE_THREAD();
                    ProcessOSEvents(impl);
                    impl.timeSystem->Tick();
                    const TimeSnapshot timeSnapshot = impl.timeSystem->GetSnapshot();
                    UpdateScene(impl, timeSnapshot.deltaSeconds);

                    ErrorCode renderResult = impl.renderSystem->RenderFrame();
                    VE_ASSERT_MESSAGE(renderResult != ErrorCode::None, "RenderFrame with error.");

                    impl.sceneThreadRenderThreadFrameEndSync->NotifySceneThreadFrameEndAndWait(
                        impl.stopRequested,
                        [&impl](UInt32 fenceIndex)
                        {
                            return impl.renderSystem->SubmitFrameEndFenceSignal(fenceIndex);
                        });
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
            if (impl.mainThreadSceneThreadFrameEndSync != nullptr)
            {
                impl.mainThreadSceneThreadFrameEndSync->UnblockAllWaiters();
            }

            if (impl.sceneThreadRenderThreadFrameEndSync != nullptr)
            {
                impl.sceneThreadRenderThreadFrameEndSync->UnblockAllWaiters();
            }

            if (impl.thread.IsJoinable())
            {
                const bool joined = impl.thread.Join();
                VE_ASSERT_MESSAGE(joined, "SceneSystem failed to join its Scene Thread during shutdown.");
            }

            impl.initialized.store(false, std::memory_order_release);
            impl.stopRequested.store(false, std::memory_order_release);
            impl.timeSystem = nullptr;
            impl.osEventQueue.ClearForConsumer();
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

    ErrorCode SceneSystem::Initialize(const SceneSystemInitParam& initParam, TimeSystem& timeSystem, RenderSystem& renderSystem)
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
        impl_->renderSystem = &renderSystem;
        impl_->stopRequested.store(false, std::memory_order_release);

        if (impl_->mainThreadSceneThreadFrameEndSync != nullptr)
        {
            impl_->mainThreadSceneThreadFrameEndSync->Reset();
        }

        if (impl_->sceneThreadRenderThreadFrameEndSync != nullptr)
        {
            impl_->sceneThreadRenderThreadFrameEndSync->Reset();
        }

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

    ErrorCode SceneSystem::EnqueueOSEvent(const OSEvent& event)
    {
        return impl_->osEventQueue.Push(event);
    }

    void SceneSystem::NotifyMainThreadFrameEnd()
    {
        impl_->mainThreadSceneThreadFrameEndSync->NotifyMainThreadFrameEnd(
            [this](UInt32 fenceIndex)
            {
                return impl_->osEventQueue.Push(
                    OSEvent{
                        OSEventType::FrameEndFenceSignal,
                        0,
                        0,
                        fenceIndex,
                    });
            });
    }

    void SceneSystem::SetMainThreadSceneThreadFrameEndSync(MainThreadSceneThreadFrameEndSync* sync) noexcept
    {
        VE_ASSERT_MESSAGE(!impl_->initialized.load(std::memory_order_acquire),
                          "SetMainThreadSceneThreadFrameEndSync requires SceneSystem to be stopped.");
        impl_->mainThreadSceneThreadFrameEndSync = sync;
    }

    void SceneSystem::SetSceneThreadRenderThreadFrameEndSync(SceneThreadRenderThreadFrameEndSync* sync) noexcept
    {
        VE_ASSERT_MESSAGE(!impl_->initialized.load(std::memory_order_acquire),
                          "SetSceneThreadRenderThreadFrameEndSync requires SceneSystem to be stopped.");
        impl_->sceneThreadRenderThreadFrameEndSync = sync;
    }


} // namespace ve
