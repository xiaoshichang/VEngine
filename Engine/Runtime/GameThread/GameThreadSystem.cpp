#include "Engine/Runtime/GameThread/GameThreadSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/ScopeExit.h"
#include "Engine/Runtime/Render/RenderSystem.h"
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
        Thread thread;
        Atomic<UInt64> gameThreadIdValue{0};
        AtomicBool initialized{false};
        AtomicBool acceptingCommands{false};
        AtomicBool stopRequested{false};
        AtomicSize activeSubmitCount{0};
        AtomicSize activeRenderFrameCount{0};
        Atomic<int> phase{static_cast<int>(GameThreadPhase::Uninitialized)};
        Atomic<UInt64> frameIndex{0};
        Atomic<UInt64> completedFrameCount{0};
        Mutex commandMutex;
        Mutex renderSystemMutex;
        Semaphore commandSemaphore{0};
        std::deque<GameThreadCommand> commands;
        RenderSystem* renderSystem = nullptr;
    };

    namespace
    {
        [[nodiscard]] RenderSystem* AcquireRenderSystem(GameThreadSystemImpl& impl)
        {
            LockGuard<Mutex> lock(impl.renderSystemMutex);
            if (impl.renderSystem == nullptr)
            {
                return nullptr;
            }

            impl.activeRenderFrameCount.fetch_add(1, std::memory_order_acq_rel);
            return impl.renderSystem;
        }

        [[nodiscard]] bool HasRenderSystem(GameThreadSystemImpl& impl)
        {
            LockGuard<Mutex> lock(impl.renderSystemMutex);
            return impl.renderSystem != nullptr;
        }

        void ExecuteRenderFrame(GameThreadSystemImpl& impl) noexcept
        {
            RenderSystem* renderSystem = AcquireRenderSystem(impl);
            if (renderSystem == nullptr)
            {
                return;
            }

            auto renderFrameCounterGuard =
                MakeScopeExit([&impl]() { impl.activeRenderFrameCount.fetch_sub(1, std::memory_order_acq_rel); });

            try
            {
                renderSystem->RenderFrame();
            }
            catch (...)
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped GameThreadSystem render-frame execution.");
            }
        }

        void RunGameFrame(GameThreadSystemImpl& impl)
        {
            const UInt64 frameId = impl.frameIndex.fetch_add(1, std::memory_order_acq_rel) + 1;
            (void)frameId;

            SetPhase(impl.phase, GameThreadPhase::BeginFrame);
            Time::Tick();

            SetPhase(impl.phase, GameThreadPhase::Lifecycle);
            SetPhase(impl.phase, GameThreadPhase::Update);
            SetPhase(impl.phase, GameThreadPhase::LateUpdate);
            SetPhase(impl.phase, GameThreadPhase::TransformUpdate);
            SetPhase(impl.phase, GameThreadPhase::RenderExtraction);
            ExecuteRenderFrame(impl);

            SetPhase(impl.phase, GameThreadPhase::EndFrame);

            impl.completedFrameCount.fetch_add(1, std::memory_order_acq_rel);
            SetPhase(impl.phase, GameThreadPhase::Idle);
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
                    LockGuard<Mutex> lock(impl.commandMutex);
                    impl.commands.push_back(std::move(command));
                }

                impl.commandSemaphore.Release();
                return ErrorCode::None;
            }
            catch (const std::bad_alloc&)
            {
                return ErrorCode::OutOfMemory;
            }
        }

        bool TryPopCommand(GameThreadSystemImpl& impl, GameThreadCommand& outCommand)
        {
            LockGuard<Mutex> lock(impl.commandMutex);
            if (impl.commands.empty())
            {
                return false;
            }

            outCommand = std::move(impl.commands.front());
            impl.commands.pop_front();
            return true;
        }

        void ClearCommands(GameThreadSystemImpl& impl)
        {
            LockGuard<Mutex> lock(impl.commandMutex);
            impl.commands.clear();
        }

        [[nodiscard]] RenderSystem* DetachRenderSystemAndWait(GameThreadSystemImpl& impl) noexcept
        {
            RenderSystem* renderSystem = nullptr;
            {
                LockGuard<Mutex> lock(impl.renderSystemMutex);
                renderSystem = impl.renderSystem;
                impl.renderSystem = nullptr;
            }

            if (GetCurrentThreadId().value == impl.gameThreadIdValue.load(std::memory_order_acquire))
            {
                return renderSystem;
            }

            while (impl.activeRenderFrameCount.load(std::memory_order_acquire) != 0)
            {
                YieldThread();
            }

            return renderSystem;
        }

        void GameThreadLoop(GameThreadSystemImpl& impl, const std::shared_ptr<ManualResetEvent>& started)
        {
            const ThreadId gameThreadId = GetCurrentThreadId();
            impl.gameThreadIdValue.store(gameThreadId.value, std::memory_order_release);
            SetPhase(impl.phase, GameThreadPhase::Idle);
            started->Set();

            for (;;)
            {
                GameThreadCommand command;
                while (TryPopCommand(impl, command))
                {
                    ExecuteCommand(command);
                }

                if (impl.stopRequested.load(std::memory_order_acquire))
                {
                    LockGuard<Mutex> lock(impl.commandMutex);
                    if (impl.commands.empty())
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

            SetPhase(impl.phase, GameThreadPhase::ShuttingDown);
        }

        void StopAndJoinGameThread(GameThreadSystemImpl& impl) noexcept
        {
            impl.acceptingCommands.store(false, std::memory_order_release);
            (void)DetachRenderSystemAndWait(impl);

            while (impl.activeSubmitCount.load(std::memory_order_acquire) != 0)
            {
                YieldThread();
            }

            impl.stopRequested.store(true, std::memory_order_release);
            impl.commandSemaphore.Release();

            if (impl.thread.IsJoinable())
            {
                const bool joined = impl.thread.Join();
                VE_ASSERT_MESSAGE(joined, "GameThreadSystem failed to join its Game Thread during shutdown.");
            }

            ClearCommands(impl);
            impl.gameThreadIdValue.store(0, std::memory_order_release);
            impl.stopRequested.store(false, std::memory_order_release);
            impl.initialized.store(false, std::memory_order_release);
            SetPhase(impl.phase, GameThreadPhase::Uninitialized);
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
        if (impl_->initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        impl_->stopRequested.store(false, std::memory_order_release);
        impl_->acceptingCommands.store(true, std::memory_order_release);
        impl_->gameThreadIdValue.store(0, std::memory_order_release);
        impl_->frameIndex.store(0, std::memory_order_release);
        impl_->completedFrameCount.store(0, std::memory_order_release);
        Time::Reset();
        SetPhase(impl_->phase, GameThreadPhase::Uninitialized);

        auto started = std::make_shared<ManualResetEvent>(false);
        ErrorCode startResult =
            impl_->thread.Start(desc.threadName.empty() ? ThreadDesc{"VEngineGameThread"} : ThreadDesc{desc.threadName},
                                [this, started]() { GameThreadLoop(*impl_, started); });

        if (startResult != ErrorCode::None)
        {
            impl_->acceptingCommands.store(false, std::memory_order_release);
            impl_->stopRequested.store(false, std::memory_order_release);
            ClearCommands(*impl_);
            return startResult;
        }

        started->Wait();
        impl_->initialized.store(true, std::memory_order_release);
        return ErrorCode::None;
    }

    void GameThreadSystem::Shutdown() noexcept
    {
        if (!impl_->initialized.load(std::memory_order_acquire))
        {
            return;
        }

        ClearRenderSystem();
        StopAndJoinGameThread(*impl_);
    }

    bool GameThreadSystem::IsInitialized() const noexcept
    {
        return impl_->initialized.load(std::memory_order_acquire);
    }

    bool GameThreadSystem::CheckGameThreadAccess() const noexcept
    {
        const ThreadId gameThreadId{impl_->gameThreadIdValue.load(std::memory_order_acquire)};
        return gameThreadId.IsValid() && GetCurrentThreadId() == gameThreadId;
    }

    void GameThreadSystem::ValidateGameThreadAccess() const noexcept
    {
        VE_ASSERT_MESSAGE(CheckGameThreadAccess(), "Live scene mutation must execute on the Game Thread.");
    }

    ThreadId GameThreadSystem::GetGameThreadId() const noexcept
    {
        return ThreadId{impl_->gameThreadIdValue.load(std::memory_order_acquire)};
    }

    GameThreadPhase GameThreadSystem::GetCurrentPhase() const noexcept
    {
        return static_cast<GameThreadPhase>(impl_->phase.load(std::memory_order_acquire));
    }

    UInt64 GameThreadSystem::GetFrameIndex() const noexcept
    {
        return impl_->frameIndex.load(std::memory_order_acquire);
    }

    UInt64 GameThreadSystem::GetCompletedFrameCount() const noexcept
    {
        return impl_->completedFrameCount.load(std::memory_order_acquire);
    }

    ErrorCode GameThreadSystem::SetRenderSystem(RenderSystem* renderSystem) noexcept
    {
        if (renderSystem == nullptr)
        {
            return ErrorCode::InvalidArgument;
        }

        if (!impl_->initialized.load(std::memory_order_acquire))
        {
            return ErrorCode::InvalidState;
        }

        {
            LockGuard<Mutex> lock(impl_->renderSystemMutex);
            renderSystem->BindGameThread(GetGameThreadId());
            impl_->renderSystem = renderSystem;
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

} // namespace ve
