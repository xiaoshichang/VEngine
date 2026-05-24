#include "Engine/Runtime/GameThread/GameThreadSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/ScopeExit.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Resource/ResourceManager.h"
#include "Engine/Runtime/Scene/Scene.h"
#include "Engine/Runtime/Scene/SceneRenderExtractor.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"
#include "Engine/Runtime/Time/Time.h"

#include <chrono>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <utility>

namespace ve
{
    namespace
    {
        using GameThreadCommandFunction = std::function<void()>;

        struct GameThreadCommand
        {
            GameThreadCommandFunction function;
        };

        void SetPhase(Atomic<int>& phase, GameThreadPhase value) noexcept
        {
            phase.store(static_cast<int>(value), std::memory_order_release);
        }
    } // namespace

    struct GameThreadSystemImpl
    {
        struct ThreadState
        {
            Thread ownedThread;
            Atomic<UInt64> gameThreadIdValue{0};
            AtomicBool initialized{false};
            AtomicBool acceptingCommands{false};
            AtomicBool stopRequested{false};
            AtomicSize activeSubmitCount{0};
        };

        struct FrameState
        {
            Atomic<int> phase{static_cast<int>(GameThreadPhase::Uninitialized)};
            Atomic<UInt64> frameIndex{0};
            Atomic<UInt64> completedFrameCount{0};
        };

        struct CommandState
        {
            Mutex mutex;
            Semaphore semaphore{0};
            std::deque<GameThreadCommand> queue;
        };

        struct RenderBridgeState
        {
            struct FrameEndSyncState
            {
                UInt32 index = 0;
                bool allowOneFrameThreadLag = true;
                RenderCommandFence fences[2];
            };

            Mutex mutex;
            AtomicSize activeRenderFrameCount{0};
            RenderSystem* renderSystem = nullptr;
            FrameEndSyncState frameEndSync;
        };

        struct SceneState
        {
            Mutex mutex;
            AtomicSize activeSceneFrameCount{0};
            Scene* activeScene = nullptr;
            ResourceManager* resourceManager = nullptr;
        };

        ThreadState thread;
        FrameState frame;
        CommandState commandQueue;
        RenderBridgeState renderBridge;
        SceneState scene;
    };

    namespace
    {
        [[nodiscard]] RenderSystem* AcquireRenderSystem(GameThreadSystemImpl& impl)
        {
            LockGuard<Mutex> lock(impl.renderBridge.mutex);
            if (impl.renderBridge.renderSystem == nullptr)
            {
                return nullptr;
            }

            impl.renderBridge.activeRenderFrameCount.fetch_add(1, std::memory_order_acq_rel);
            return impl.renderBridge.renderSystem;
        }

        [[nodiscard]] bool HasRenderSystem(GameThreadSystemImpl& impl)
        {
            LockGuard<Mutex> lock(impl.renderBridge.mutex);
            return impl.renderBridge.renderSystem != nullptr;
        }

        [[nodiscard]] bool
        AcquireActiveScene(GameThreadSystemImpl& impl, Scene*& outScene, ResourceManager*& outResourceManager)
        {
            LockGuard<Mutex> lock(impl.scene.mutex);
            if (impl.scene.activeScene == nullptr || impl.scene.resourceManager == nullptr)
            {
                outScene = nullptr;
                outResourceManager = nullptr;
                return false;
            }

            impl.scene.activeSceneFrameCount.fetch_add(1, std::memory_order_acq_rel);
            outScene = impl.scene.activeScene;
            outResourceManager = impl.scene.resourceManager;
            return true;
        }

        void ReleaseActiveScene(GameThreadSystemImpl& impl) noexcept
        {
            impl.scene.activeSceneFrameCount.fetch_sub(1, std::memory_order_acq_rel);
        }

        void UpdateActiveScene(GameThreadSystemImpl& impl)
        {
            Scene* scene = nullptr;
            ResourceManager* resourceManager = nullptr;
            if (!AcquireActiveScene(impl, scene, resourceManager))
            {
                return;
            }

            auto sceneGuard = MakeScopeExit([&impl]() { ReleaseActiveScene(impl); });
            scene->Update();
        }

        void LateUpdateActiveScene(GameThreadSystemImpl& impl)
        {
            Scene* scene = nullptr;
            ResourceManager* resourceManager = nullptr;
            if (!AcquireActiveScene(impl, scene, resourceManager))
            {
                return;
            }

            auto sceneGuard = MakeScopeExit([&impl]() { ReleaseActiveScene(impl); });
            scene->LateUpdate();
        }

        void UpdateActiveSceneTransforms(GameThreadSystemImpl& impl)
        {
            Scene* scene = nullptr;
            ResourceManager* resourceManager = nullptr;
            if (!AcquireActiveScene(impl, scene, resourceManager))
            {
                return;
            }

            auto sceneGuard = MakeScopeExit([&impl]() { ReleaseActiveScene(impl); });
            scene->UpdateTransforms();
        }

        void ExecuteRenderFrame(GameThreadSystemImpl& impl) noexcept
        {
            RenderSystem* renderSystem = AcquireRenderSystem(impl);
            if (renderSystem == nullptr)
            {
                return;
            }

            auto renderFrameCounterGuard = MakeScopeExit(
                [&impl]() { impl.renderBridge.activeRenderFrameCount.fetch_sub(1, std::memory_order_acq_rel); });

            try
            {
                renderSystem->RenderFrame();
            }
            catch (...)
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped GameThreadSystem render-frame execution.");
            }
        }

        void ExecuteRenderExtraction(GameThreadSystemImpl& impl, UInt64 frameId) noexcept
        {
            RenderSystem* renderSystem = AcquireRenderSystem(impl);
            if (renderSystem == nullptr)
            {
                return;
            }

            auto renderFrameCounterGuard = MakeScopeExit(
                [&impl]() { impl.renderBridge.activeRenderFrameCount.fetch_sub(1, std::memory_order_acq_rel); });

            Scene* scene = nullptr;
            ResourceManager* resourceManager = nullptr;
            const bool hasScene = AcquireActiveScene(impl, scene, resourceManager);
            auto sceneFrameCounterGuard = MakeScopeExit(
                [&impl, hasScene]()
                {
                    if (hasScene)
                    {
                        ReleaseActiveScene(impl);
                    }
                });

            try
            {
                if (hasScene)
                {
                    SceneRenderSnapshot snapshot = ExtractSceneRenderSnapshot(*scene, *resourceManager, frameId);
                    renderSystem->SubmitFrame(std::move(snapshot));
                }
                else
                {
                    renderSystem->RenderFrame();
                }
            }
            catch (...)
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped GameThreadSystem render extraction.");
            }
        }

        void SyncFrameEnd(GameThreadSystemImpl& impl) noexcept
        {
            RenderSystem* renderSystem = AcquireRenderSystem(impl);
            if (renderSystem == nullptr)
            {
                return;
            }

            auto renderFrameCounterGuard = MakeScopeExit(
                [&impl]() { impl.renderBridge.activeRenderFrameCount.fetch_sub(1, std::memory_order_acq_rel); });

            try
            {
                renderSystem->BeginFrameEndFence(
                    impl.renderBridge.frameEndSync.fences[impl.renderBridge.frameEndSync.index]);

                if (impl.renderBridge.frameEndSync.allowOneFrameThreadLag)
                {
                    impl.renderBridge.frameEndSync.index = (impl.renderBridge.frameEndSync.index + 1) % 2;
                }

                impl.renderBridge.frameEndSync.fences[impl.renderBridge.frameEndSync.index].Wait();
            }
            catch (...)
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped GameThreadSystem frame-end sync.");
            }
        }

        void RunGameFrame(GameThreadSystemImpl& impl)
        {
            const UInt64 frameId = impl.frame.frameIndex.fetch_add(1, std::memory_order_acq_rel) + 1;
            (void)frameId;

            SetPhase(impl.frame.phase, GameThreadPhase::BeginFrame);
            Time::Tick();

            SetPhase(impl.frame.phase, GameThreadPhase::Lifecycle);
            SetPhase(impl.frame.phase, GameThreadPhase::Update);
            UpdateActiveScene(impl);
            SetPhase(impl.frame.phase, GameThreadPhase::LateUpdate);
            LateUpdateActiveScene(impl);
            SetPhase(impl.frame.phase, GameThreadPhase::TransformUpdate);
            UpdateActiveSceneTransforms(impl);
            SetPhase(impl.frame.phase, GameThreadPhase::RenderExtraction);
            ExecuteRenderExtraction(impl, frameId);

            SetPhase(impl.frame.phase, GameThreadPhase::EndFrame);
            SyncFrameEnd(impl);

            impl.frame.completedFrameCount.fetch_add(1, std::memory_order_acq_rel);
            SetPhase(impl.frame.phase, GameThreadPhase::Idle);
        }

        void ExecuteCommand(GameThreadCommand& command) noexcept
        {
            try
            {
                command.function();
            }
            catch (...)
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped a GameThreadSystem command.");
            }
        }

        [[nodiscard]] ErrorCode PushCommand(GameThreadSystemImpl& impl, GameThreadCommand command)
        {
            try
            {
                {
                    LockGuard<Mutex> lock(impl.commandQueue.mutex);
                    impl.commandQueue.queue.push_back(std::move(command));
                }

                impl.commandQueue.semaphore.Release();
                return ErrorCode::None;
            }
            catch (const std::bad_alloc&)
            {
                return ErrorCode::OutOfMemory;
            }
        }

        bool TryPopCommand(GameThreadSystemImpl& impl, GameThreadCommand& outCommand)
        {
            LockGuard<Mutex> lock(impl.commandQueue.mutex);
            if (impl.commandQueue.queue.empty())
            {
                return false;
            }

            outCommand = std::move(impl.commandQueue.queue.front());
            impl.commandQueue.queue.pop_front();
            return true;
        }

        void ClearCommands(GameThreadSystemImpl& impl)
        {
            LockGuard<Mutex> lock(impl.commandQueue.mutex);
            impl.commandQueue.queue.clear();
        }

        [[nodiscard]] RenderSystem* DetachRenderSystemAndWait(GameThreadSystemImpl& impl) noexcept
        {
            RenderSystem* renderSystem = nullptr;
            {
                LockGuard<Mutex> lock(impl.renderBridge.mutex);
                renderSystem = impl.renderBridge.renderSystem;
                impl.renderBridge.renderSystem = nullptr;
            }

            if (GetCurrentThreadId().value == impl.thread.gameThreadIdValue.load(std::memory_order_acquire))
            {
                return renderSystem;
            }

            while (impl.renderBridge.activeRenderFrameCount.load(std::memory_order_acquire) != 0)
            {
                YieldThread();
            }

            return renderSystem;
        }

        void DetachActiveSceneAndWait(GameThreadSystemImpl& impl) noexcept
        {
            {
                LockGuard<Mutex> lock(impl.scene.mutex);
                impl.scene.activeScene = nullptr;
                impl.scene.resourceManager = nullptr;
            }

            if (GetCurrentThreadId().value == impl.thread.gameThreadIdValue.load(std::memory_order_acquire))
            {
                return;
            }

            while (impl.scene.activeSceneFrameCount.load(std::memory_order_acquire) != 0)
            {
                YieldThread();
            }
        }

        void GameThreadLoop(GameThreadSystemImpl& impl, const std::shared_ptr<ManualResetEvent>& started)
        {
            const ThreadId gameThreadId = GetCurrentThreadId();
            impl.thread.gameThreadIdValue.store(gameThreadId.value, std::memory_order_release);
            SetPhase(impl.frame.phase, GameThreadPhase::Idle);
            started->Set();

            for (;;)
            {
                GameThreadCommand command;
                while (TryPopCommand(impl, command))
                {
                    ExecuteCommand(command);
                }

                if (impl.thread.stopRequested.load(std::memory_order_acquire))
                {
                    LockGuard<Mutex> lock(impl.commandQueue.mutex);
                    if (impl.commandQueue.queue.empty())
                    {
                        break;
                    }

                    continue;
                }

                RunGameFrame(impl);

                if (!HasRenderSystem(impl))
                {
                    SleepFor(std::chrono::milliseconds(1));
                }
            }

            GameThreadCommand command;
            while (TryPopCommand(impl, command))
            {
                ExecuteCommand(command);
            }

            SetPhase(impl.frame.phase, GameThreadPhase::ShuttingDown);
        }

        void StopAndJoinGameThread(GameThreadSystemImpl& impl) noexcept
        {
            impl.thread.acceptingCommands.store(false, std::memory_order_release);
            DetachActiveSceneAndWait(impl);
            (void)DetachRenderSystemAndWait(impl);

            while (impl.thread.activeSubmitCount.load(std::memory_order_acquire) != 0)
            {
                YieldThread();
            }

            impl.thread.stopRequested.store(true, std::memory_order_release);
            impl.commandQueue.semaphore.Release();

            if (impl.thread.ownedThread.IsJoinable())
            {
                const bool joined = impl.thread.ownedThread.Join();
                VE_ASSERT_MESSAGE(joined, "GameThreadSystem failed to join its Game Thread during shutdown.");
            }

            ClearCommands(impl);
            impl.thread.gameThreadIdValue.store(0, std::memory_order_release);
            impl.thread.stopRequested.store(false, std::memory_order_release);
            impl.thread.initialized.store(false, std::memory_order_release);
            SetPhase(impl.frame.phase, GameThreadPhase::Uninitialized);
        }
    } // namespace

    GameThreadSystem::GameThreadSystem()
        : impl_(std::make_unique<GameThreadSystemImpl>())
    {
    }

    GameThreadSystem::~GameThreadSystem()
    {
        Shutdown();
    }

    ErrorCode GameThreadSystem::Initialize(const GameThreadSystemDesc& desc)
    {
        if (impl_->thread.initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        impl_->thread.stopRequested.store(false, std::memory_order_release);
        impl_->thread.acceptingCommands.store(true, std::memory_order_release);
        impl_->thread.gameThreadIdValue.store(0, std::memory_order_release);
        impl_->frame.frameIndex.store(0, std::memory_order_release);
        impl_->frame.completedFrameCount.store(0, std::memory_order_release);
        Time::Reset();
        SetPhase(impl_->frame.phase, GameThreadPhase::Uninitialized);

        auto started = std::make_shared<ManualResetEvent>(false);
        ErrorCode startResult = impl_->thread.ownedThread.Start(
            desc.threadName.empty() ? ThreadDesc{"VEngineGameThread"} : ThreadDesc{desc.threadName},
            [this, started]() { GameThreadLoop(*impl_, started); });

        if (startResult != ErrorCode::None)
        {
            impl_->thread.acceptingCommands.store(false, std::memory_order_release);
            impl_->thread.stopRequested.store(false, std::memory_order_release);
            ClearCommands(*impl_);
            return startResult;
        }

        started->Wait();
        impl_->thread.initialized.store(true, std::memory_order_release);
        return ErrorCode::None;
    }

    void GameThreadSystem::Shutdown() noexcept
    {
        if (!impl_->thread.initialized.load(std::memory_order_acquire))
        {
            return;
        }

        ClearRenderSystem();
        StopAndJoinGameThread(*impl_);
    }

    bool GameThreadSystem::IsInitialized() const noexcept
    {
        return impl_->thread.initialized.load(std::memory_order_acquire);
    }

    bool GameThreadSystem::CheckGameThreadAccess() const noexcept
    {
        const ThreadId gameThreadId{impl_->thread.gameThreadIdValue.load(std::memory_order_acquire)};
        return gameThreadId.IsValid() && GetCurrentThreadId() == gameThreadId;
    }

    void GameThreadSystem::ValidateGameThreadAccess() const noexcept
    {
        VE_ASSERT_MESSAGE(CheckGameThreadAccess(), "Live scene mutation must execute on the Game Thread.");
    }

    ThreadId GameThreadSystem::GetGameThreadId() const noexcept
    {
        return ThreadId{impl_->thread.gameThreadIdValue.load(std::memory_order_acquire)};
    }

    GameThreadPhase GameThreadSystem::GetCurrentPhase() const noexcept
    {
        return static_cast<GameThreadPhase>(impl_->frame.phase.load(std::memory_order_acquire));
    }

    UInt64 GameThreadSystem::GetFrameIndex() const noexcept
    {
        return impl_->frame.frameIndex.load(std::memory_order_acquire);
    }

    UInt64 GameThreadSystem::GetCompletedFrameCount() const noexcept
    {
        return impl_->frame.completedFrameCount.load(std::memory_order_acquire);
    }

    ErrorCode GameThreadSystem::SetRenderSystem(RenderSystem* renderSystem) noexcept
    {
        if (renderSystem == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }

        if (!impl_->thread.initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        {
            LockGuard<Mutex> lock(impl_->renderBridge.mutex);
            renderSystem->BindGameThread(GetGameThreadId());
            impl_->renderBridge.renderSystem = renderSystem;
        }

        return ErrorCode::None;
    }

    void GameThreadSystem::ClearRenderSystem() noexcept
    {
        RenderSystem* renderSystem = DetachRenderSystemAndWait(*impl_);
        if (renderSystem != nullptr)
        {
            renderSystem->ClearGameThreadBinding();
        }
    }

    ErrorCode GameThreadSystem::SetActiveScene(Scene* scene, ResourceManager* resourceManager) noexcept
    {
        if (scene == nullptr || resourceManager == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }

        if (!impl_->thread.initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        {
            LockGuard<Mutex> lock(impl_->scene.mutex);
            impl_->scene.activeScene = scene;
            impl_->scene.resourceManager = resourceManager;
        }

        return ErrorCode::None;
    }

    void GameThreadSystem::ClearActiveScene() noexcept
    {
        DetachActiveSceneAndWait(*impl_);
    }

} // namespace ve
