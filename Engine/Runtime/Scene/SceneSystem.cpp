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
        InputSystem* inputSystem = nullptr;
        RenderSystem* renderSystem = nullptr;

        // frame sync between main thread and render thread.
        ManualResetEvent startLoopEvent;
        MainThreadSceneThreadFrameEndSync* mainThreadSceneThreadFrameEndSync = nullptr;
        SceneThreadRenderThreadFrameEndSync* sceneThreadRenderThreadFrameEndSync = nullptr;
        SceneSystemEditorCallback editorCallback;
        std::function<void(const OSEvent& event)> runtimeOSEventCallback;

        AtomicBool initialized{false};
        AtomicBool stopRequested{false};
    };

    namespace
    {
        void ProcessOSEvents(SceneSystemImpl& impl)
        {
            const auto runtimeOnOSEvent = impl.runtimeOSEventCallback;
            const auto editorOnOSEvent = impl.editorCallback.onOSEvent;
            OSEvent event;
            while (impl.osEventQueue.TryPop(event))
            {
                if (event.type == OSEventType::FrameEndFenceSignal)
                {
                    if (impl.mainThreadSceneThreadFrameEndSync != nullptr)
                    {
                        impl.mainThreadSceneThreadFrameEndSync->NotifySceneThreadFrameEnd(event.fenceIndex);
                    }

                    continue;
                }

                if (runtimeOnOSEvent != nullptr)
                {
                    runtimeOnOSEvent(event);
                }

                bool shouldDispatchToInput = true;
                if (editorOnOSEvent != nullptr)
                {
                    shouldDispatchToInput = editorOnOSEvent(event);
                }
                if (shouldDispatchToInput)
                {
                    impl.inputSystem->ProcessOSEvent(event);
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


        void SceneThreadLoop_StartFrame(SceneSystemImpl& impl)
        {
            if (impl.editorCallback.onStartFrame != nullptr)
            {
                impl.editorCallback.onStartFrame();
            }
            if (impl.inputSystem != nullptr)
            {
                impl.inputSystem->BeginFrame();
            }
            ProcessOSEvents(impl);
        }


        [[nodiscard]] std::shared_ptr<FrameRenderer> CreatePlayerRenderer(SceneSystemImpl& impl)
        {
            VE_ASSERT_SCENE_THREAD();
            VE_ASSERT(impl.renderSystem != nullptr);

            auto renderer = std::make_shared<FrameRenderer>();
            renderer->AddPass(impl.renderSystem->CreateTriangleForwardPass());
            return renderer;
        }

        void SceneThreadLoop_Render_Editor(SceneSystemImpl& impl)
        {
            VE_ASSERT_SCENE_THREAD();
            VE_ASSERT(impl.renderSystem != nullptr);
            VE_ASSERT(impl.editorCallback.onRender != nullptr);

            std::shared_ptr<FrameRenderer> renderer = impl.editorCallback.onRender();
            VE_ASSERT_MESSAGE(renderer != nullptr, "SceneThreadLoop_Render_Editor requires a renderer.");

            const ErrorCode renderResult = impl.renderSystem->RenderFrame(std::move(renderer));
            VE_ASSERT_MESSAGE(renderResult == ErrorCode::None, "SceneThreadLoop_Render_Editor failed.");
        }

        void SceneThreadLoop_Render_Player(SceneSystemImpl& impl)
        {
            VE_ASSERT_SCENE_THREAD();
            VE_ASSERT(impl.renderSystem != nullptr);

            const ErrorCode renderResult = impl.renderSystem->RenderFrame(CreatePlayerRenderer(impl));
            VE_ASSERT_MESSAGE(renderResult == ErrorCode::None, "SceneThreadLoop_Render_Player failed.");
        }

        void SceneThreadLoop_Render(SceneSystemImpl& impl)
        {
            if (impl.editorCallback.onRender != nullptr)
            {
                SceneThreadLoop_Render_Editor(impl);
            }
            else
            {
                SceneThreadLoop_Render_Player(impl);
            }
        }

        void SceneThreadLoop_EndFrame(SceneSystemImpl& impl)
        {
            impl.sceneThreadRenderThreadFrameEndSync->NotifySceneThreadFrameEndAndWait(
                impl.stopRequested,
                [&impl](UInt32 fenceIndex) { return impl.renderSystem->SubmitFrameEndFenceSignal(fenceIndex); });
        }

        void SceneThreadLoop(SceneSystemImpl& impl)
        {
            const ThreadId sceneThreadId = GetCurrentThreadId();
            impl.sceneThreadIdValue.store(sceneThreadId.value, std::memory_order_release);
            SetExpectedSceneThreadId(sceneThreadId);
            impl.startLoopEvent.Wait();

            VE_ASSERT_MESSAGE(impl.timeSystem != nullptr, "impl.timeSystem should not be nullptr");
            VE_ASSERT_MESSAGE(impl.timeSystem->IsInitialized(), "impl.timeSystem should be initialized.");

            while (!impl.stopRequested.load(std::memory_order_acquire))
            {
                try
                {
                    SceneThreadLoop_StartFrame(impl);

                    impl.timeSystem->Tick();
                    const TimeSnapshot timeSnapshot = impl.timeSystem->GetSnapshot();
                    UpdateScene(impl, timeSnapshot.deltaSeconds);

                    SceneThreadLoop_Render(impl);
                    SceneThreadLoop_EndFrame(impl);
                    
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
            impl.startLoopEvent.Set();
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
            impl.inputSystem = nullptr;
            if (impl.scene != nullptr)
            {
                impl.scene->Clear();
                if (impl.renderSystem != nullptr)
                {
                    [[maybe_unused]] const ErrorCode flushResult = impl.renderSystem->Flush();
                    VE_ASSERT_MESSAGE(flushResult == ErrorCode::None,
                                      "SceneSystem failed to flush RTScene shutdown commands.");
                }
                impl.scene->SetSceneSystem(nullptr);
            }
            impl.renderSystem = nullptr;
            impl.runtimeOSEventCallback = nullptr;
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

    ErrorCode SceneSystem::Initialize(const SceneSystemInitParam& initParam,
                                      TimeSystem& timeSystem,
                                      InputSystem& inputSystem,
                                      RenderSystem& renderSystem)
    {
        if (impl_->initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        if (!timeSystem.IsInitialized())
        {
            return ErrorCode::InvalidState;
        }

        if (!inputSystem.IsInitialized())
        {
            return ErrorCode::InvalidState;
        }

        if (!renderSystem.IsInitialized())
        {
            return ErrorCode::InvalidState;
        }

        impl_->timeSystem = &timeSystem;
        impl_->inputSystem = &inputSystem;
        impl_->renderSystem = &renderSystem;
        impl_->stopRequested.store(false, std::memory_order_release);
        impl_->startLoopEvent.Reset();
        if (impl_->scene == nullptr)
        {
            impl_->scene = std::make_unique<Scene>();
        }
        impl_->scene->SetSceneSystem(this);

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

    ErrorCode SceneSystem::EnqueueRenderCommand(RenderCommand command)
    {
        if (!HasRenderSystem())
        {
            return ErrorCode::InvalidState;
        }

        return impl_->renderSystem->EnqueueCommand(std::move(command));
    }

    bool SceneSystem::HasRenderSystem() const noexcept
    {
        return impl_->renderSystem != nullptr && impl_->renderSystem->IsInitialized();
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

    void SceneSystem::SetEditorCallback(SceneSystemEditorCallback callback) noexcept
    {
        impl_->editorCallback = std::move(callback);
    }

    void SceneSystem::SetRuntimeOSEventCallback(std::function<void(const OSEvent& event)> callback) noexcept
    {
        impl_->runtimeOSEventCallback = std::move(callback);
    }

    void SceneSystem::StartLoop() noexcept
    {
        impl_->startLoopEvent.Set();
    }

} // namespace ve
