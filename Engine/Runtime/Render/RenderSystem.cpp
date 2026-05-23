#include "Engine/Runtime/Render/RenderSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Core/ScopeExit.h"
#include "Engine/Runtime/Render/RenderCommandQueue.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"

#include <exception>

namespace ve
{
struct RenderSystemImpl
{
    Thread thread;
    Atomic<UInt64> renderThreadIdValue{0};
    Semaphore commandSemaphore{0};
    RenderCommandQueue commandQueue;
    AtomicBool acceptingCommands{false};
    AtomicBool stopRequested{false};
    AtomicBool initialized{false};
    AtomicSize activeSubmitCount{0};
};

namespace
{
void ExecuteCommand(RenderThreadContext& context, RenderCommand& command) noexcept
{
    try
    {
        command.function(context);
    }
    catch (...)
    {
        VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped a RenderSystem command.");
    }
}

void RenderThreadLoop(RenderSystemImpl& impl)
{
    const ThreadId renderThreadId = GetCurrentThreadId();
    impl.renderThreadIdValue.store(renderThreadId.value, std::memory_order_release);
    RenderThreadContext context(renderThreadId);

    for (;;)
    {
        while (std::optional<RenderCommand> command = impl.commandQueue.TryPop())
        {
            ExecuteCommand(context, *command);
        }

        if (impl.stopRequested.load(std::memory_order_acquire))
        {
            if (!impl.commandQueue.IsEmptyForConsumer())
            {
                continue;
            }

            break;
        }

        impl.commandSemaphore.Acquire();
    }

    while (std::optional<RenderCommand> command = impl.commandQueue.TryPop())
    {
        ExecuteCommand(context, *command);
    }
}

void StopAndJoinRenderThread(RenderSystemImpl& impl) noexcept
{
    impl.acceptingCommands.store(false, std::memory_order_release);

    while (impl.activeSubmitCount.load(std::memory_order_acquire) != 0)
    {
        YieldThread();
    }

    impl.stopRequested.store(true, std::memory_order_release);
    impl.commandSemaphore.Release();

    if (impl.thread.IsJoinable())
    {
        const bool joined = impl.thread.Join();
        VE_ASSERT_MESSAGE(joined, "RenderSystem failed to join its Render Thread during shutdown.");
    }

    impl.commandQueue.ClearForConsumer();
    impl.renderThreadIdValue.store(0, std::memory_order_release);
    impl.stopRequested.store(false, std::memory_order_release);
    impl.initialized.store(false, std::memory_order_release);
}
}

RenderThreadContext::RenderThreadContext(ThreadId renderThreadId) noexcept
    : renderThreadId_(renderThreadId)
{
}

ThreadId RenderThreadContext::GetRenderThreadId() const noexcept
{
    return renderThreadId_;
}

RenderSystem::RenderSystem()
    : impl_(std::make_unique<RenderSystemImpl>())
{
}

RenderSystem::~RenderSystem()
{
    Shutdown();
}

Result<void> RenderSystem::Initialize(const RenderSystemDesc& desc)
{
    if (impl_->initialized.load(std::memory_order_acquire))
    {
        return Result<void>::Failure(Error(ErrorCode::InvalidState, "RenderSystem is already initialized."));
    }

    impl_->stopRequested.store(false, std::memory_order_release);
    impl_->acceptingCommands.store(true, std::memory_order_release);

    Result<void> startResult = impl_->thread.Start(desc.threadName.empty() ? ThreadDesc{"VEngineRenderThread"}
                                                                           : ThreadDesc{desc.threadName},
        [this]()
        {
            RenderThreadLoop(*impl_);
        });

    if (!startResult)
    {
        impl_->acceptingCommands.store(false, std::memory_order_release);
        impl_->stopRequested.store(false, std::memory_order_release);
        impl_->commandQueue.ClearForConsumer();
        return startResult;
    }

    impl_->initialized.store(true, std::memory_order_release);
    return Result<void>::Success();
}

void RenderSystem::Shutdown() noexcept
{
    if (!impl_->initialized.load(std::memory_order_acquire))
    {
        return;
    }

    StopAndJoinRenderThread(*impl_);
}

bool RenderSystem::IsInitialized() const noexcept
{
    return impl_->initialized.load(std::memory_order_acquire);
}

ThreadId RenderSystem::GetRenderThreadId() const noexcept
{
    return ThreadId{impl_->renderThreadIdValue.load(std::memory_order_acquire)};
}

Result<void> RenderSystem::Submit(RenderCommand command)
{
    return SubmitFunction(std::move(command.debugName), std::move(command.function));
}

Result<void> RenderSystem::Flush()
{
    if (!impl_->acceptingCommands.load(std::memory_order_acquire))
    {
        return Result<void>::Failure(Error(ErrorCode::InvalidState, "RenderSystem is not accepting commands."));
    }

    auto completed = std::make_shared<ManualResetEvent>(false);
    Result<void> submitResult = SubmitFunction(
        "RenderSystemFlush",
        [completed](RenderThreadContext&)
        {
            completed->Set();
        });

    if (!submitResult)
    {
        return submitResult;
    }

    completed->Wait();
    return Result<void>::Success();
}

Result<void> RenderSystem::SubmitFunction(std::string debugName, RenderCommandFunction function)
{
    if (!function)
    {
        return Result<void>::Failure(Error(ErrorCode::InvalidArgument, "Render command requires a callable function."));
    }

    impl_->activeSubmitCount.fetch_add(1, std::memory_order_acq_rel);
    auto submitCounterGuard = MakeScopeExit(
        [this]()
        {
            impl_->activeSubmitCount.fetch_sub(1, std::memory_order_acq_rel);
        });

    if (!impl_->acceptingCommands.load(std::memory_order_acquire))
    {
        return Result<void>::Failure(Error(ErrorCode::InvalidState, "RenderSystem is not accepting commands."));
    }

    Result<void> pushResult = impl_->commandQueue.Push(RenderCommand{std::move(debugName), std::move(function)});
    if (!pushResult)
    {
        return pushResult;
    }

    impl_->commandSemaphore.Release();
    return Result<void>::Success();
}
}
