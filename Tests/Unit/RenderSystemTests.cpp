#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <array>
#include <iostream>

namespace
{
bool Expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAILED: " << message << '\n';
    }

    return condition;
}

bool ExpectOk(const ve::Result<void>& result, const char* message)
{
    if (result)
    {
        return true;
    }

    std::cerr << "FAILED: " << message << ": " << ve::ToString(result.GetError().GetCode());

    if (!result.GetError().GetMessage().empty())
    {
        std::cerr << ": " << result.GetError().GetMessage();
    }

    std::cerr << '\n';
    return false;
}

ve::RenderSystemDesc MakeRenderSystemDesc()
{
    ve::RenderSystemDesc desc;
    desc.threadName = "RenderSystemTestThread";
    return desc;
}

bool WaitForValue(const ve::AtomicInt32& value, int expected)
{
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        if (value.load(std::memory_order_acquire) == expected)
        {
            return true;
        }

        ve::SleepFor(std::chrono::milliseconds(1));
    }

    return false;
}

bool TestInitializeAndShutdown()
{
    bool passed = true;

    ve::RenderSystem renderSystem;
    passed &= Expect(!renderSystem.IsInitialized(), "New RenderSystem should not be initialized");
    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "RenderSystem should initialize");
    passed &= Expect(renderSystem.IsInitialized(), "Initialized RenderSystem should report initialized");

    passed &= ExpectOk(renderSystem.Flush(), "Flush should succeed while RenderSystem is initialized");
    passed &= Expect(renderSystem.GetRenderThreadId().IsValid(), "RenderSystem should expose a valid Render Thread id");

    renderSystem.Shutdown();
    passed &= Expect(!renderSystem.IsInitialized(), "Shutdown RenderSystem should report uninitialized");

    renderSystem.Shutdown();
    passed &= Expect(!renderSystem.IsInitialized(), "Repeated Shutdown should remain a no-op");

    return passed;
}

bool TestRepeatedInitializeWhileRunningFails()
{
    bool passed = true;

    ve::RenderSystem renderSystem;
    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "Initial RenderSystem Initialize should succeed");

    const ve::Result<void> repeatedInitialize = renderSystem.Initialize(MakeRenderSystemDesc());
    passed &= Expect(!repeatedInitialize, "Repeated RenderSystem Initialize while running should fail");
    if (!repeatedInitialize)
    {
        passed &= Expect(
            repeatedInitialize.GetError().GetCode() == ve::ErrorCode::InvalidState,
            "Repeated RenderSystem Initialize should report InvalidState");
    }

    renderSystem.Shutdown();
    return passed;
}

bool TestInitializeAfterShutdownSucceeds()
{
    bool passed = true;

    ve::RenderSystem renderSystem;
    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "Initial RenderSystem lifecycle should initialize");
    renderSystem.Shutdown();
    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "Standalone RenderSystem should initialize again");
    renderSystem.Shutdown();

    return passed;
}

bool TestSubmitBeforeInitializeAndAfterShutdownFails()
{
    bool passed = true;

    ve::RenderSystem renderSystem;
    ve::RenderCommand command;
    command.debugName = "ShouldNotRun";
    command.function = [](ve::RenderThreadContext&)
    {
    };

    ve::Result<void> beforeInitialize = renderSystem.Submit(std::move(command));
    passed &= Expect(!beforeInitialize, "Submit before Initialize should fail");
    if (!beforeInitialize)
    {
        passed &= Expect(
            beforeInitialize.GetError().GetCode() == ve::ErrorCode::InvalidState,
            "Submit before Initialize should report InvalidState");
    }

    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "RenderSystem should initialize");
    renderSystem.Shutdown();

    ve::RenderCommand afterShutdownCommand;
    afterShutdownCommand.debugName = "ShouldNotRunAfterShutdown";
    afterShutdownCommand.function = [](ve::RenderThreadContext&)
    {
    };

    ve::Result<void> afterShutdown = renderSystem.Submit(std::move(afterShutdownCommand));
    passed &= Expect(!afterShutdown, "Submit after Shutdown should fail until reinitialized");
    if (!afterShutdown)
    {
        passed &= Expect(
            afterShutdown.GetError().GetCode() == ve::ErrorCode::InvalidState,
            "Submit after Shutdown should report InvalidState");
    }

    return passed;
}

bool TestEmptyCommandFails()
{
    bool passed = true;

    ve::RenderSystem renderSystem;
    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "RenderSystem should initialize");

    ve::RenderCommand command;
    command.debugName = "EmptyCommand";

    const ve::Result<void> result = renderSystem.Submit(std::move(command));
    passed &= Expect(!result, "Submitting an empty render command should fail");
    if (!result)
    {
        passed &= Expect(
            result.GetError().GetCode() == ve::ErrorCode::InvalidArgument,
            "Empty render command should report InvalidArgument");
    }

    renderSystem.Shutdown();
    return passed;
}

bool TestCommandExecutesOnRenderThread()
{
    bool passed = true;

    ve::RenderSystem renderSystem;
    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "RenderSystem should initialize for command test");

    const ve::ThreadId submitterThreadId = ve::GetCurrentThreadId();
    ve::ThreadId executedThreadId;
    ve::ThreadId contextThreadId;
    ve::AtomicInt32 executed{0};

    ve::RenderCommand command;
    command.debugName = "RecordThreadIds";
    command.function = [&](ve::RenderThreadContext& context)
    {
        executedThreadId = ve::GetCurrentThreadId();
        contextThreadId = context.GetRenderThreadId();
        executed.store(1, std::memory_order_release);
    };

    passed &= ExpectOk(renderSystem.Submit(std::move(command)), "Render command should be accepted");
    passed &= ExpectOk(renderSystem.Flush(), "Flush should wait for command execution");

    passed &= Expect(executed.load(std::memory_order_acquire) == 1, "Render command should execute");
    passed &= Expect(executedThreadId.IsValid(), "Executed thread id should be valid");
    passed &= Expect(contextThreadId.IsValid(), "Context thread id should be valid");
    passed &= Expect(executedThreadId == contextThreadId, "Context should identify the executing Render Thread");
    passed &= Expect(executedThreadId != submitterThreadId, "Render command should execute away from submitter thread");

    renderSystem.Shutdown();
    return passed;
}

bool TestFlushWaitsForAcceptedCommands()
{
    bool passed = true;

    ve::RenderSystem renderSystem;
    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "RenderSystem should initialize for Flush test");

    ve::AtomicInt32 counter{0};
    constexpr int CommandCount = 32;

    for (int commandIndex = 0; commandIndex < CommandCount; ++commandIndex)
    {
        ve::RenderCommand command;
        command.debugName = "IncrementCounter";
        command.function = [&](ve::RenderThreadContext&)
        {
            counter.fetch_add(1, std::memory_order_acq_rel);
        };

        passed &= ExpectOk(renderSystem.Submit(std::move(command)), "Render command should be accepted before Flush");
    }

    passed &= ExpectOk(renderSystem.Flush(), "Flush should succeed");
    passed &= Expect(
        counter.load(std::memory_order_acquire) == CommandCount,
        "Flush should wait for all previously accepted commands");

    renderSystem.Shutdown();
    return passed;
}

bool TestShutdownDrainsAcceptedCommands()
{
    bool passed = true;

    ve::RenderSystem renderSystem;
    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "RenderSystem should initialize for drain test");

    ve::ManualResetEvent releaseCommand(false);
    ve::AtomicInt32 counter{0};

    ve::RenderCommand blockingCommand;
    blockingCommand.debugName = "BlockingCommand";
    blockingCommand.function = [&](ve::RenderThreadContext&)
    {
        releaseCommand.Wait();
        counter.fetch_add(1, std::memory_order_acq_rel);
    };

    passed &= ExpectOk(renderSystem.Submit(std::move(blockingCommand)), "Blocking render command should be accepted");

    constexpr int TrailingCommandCount = 8;
    for (int commandIndex = 0; commandIndex < TrailingCommandCount; ++commandIndex)
    {
        ve::RenderCommand command;
        command.debugName = "TrailingCommand";
        command.function = [&](ve::RenderThreadContext&)
        {
            counter.fetch_add(1, std::memory_order_acq_rel);
        };

        passed &= ExpectOk(renderSystem.Submit(std::move(command)), "Trailing render command should be accepted");
    }

    ve::Thread shutdownThread;
    passed &= ExpectOk(
        shutdownThread.Start(ve::ThreadDesc{"RenderSystemShutdownThread"}, [&]()
        {
            renderSystem.Shutdown();
        }),
        "Shutdown helper thread should start");

    ve::SleepFor(std::chrono::milliseconds(10));
    passed &= Expect(counter.load(std::memory_order_acquire) == 0, "Shutdown should wait for blocked accepted command");

    releaseCommand.Set();
    passed &= Expect(shutdownThread.Join(), "Shutdown helper thread should join");

    passed &= Expect(
        counter.load(std::memory_order_acquire) == TrailingCommandCount + 1,
        "Shutdown should drain accepted render commands before returning");
    passed &= Expect(!renderSystem.IsInitialized(), "RenderSystem should report uninitialized after shutdown drain");

    return passed;
}

bool TestMultipleProducerThreadsSubmitCommands()
{
    bool passed = true;

    ve::RenderSystem renderSystem;
    passed &= ExpectOk(renderSystem.Initialize(MakeRenderSystemDesc()), "RenderSystem should initialize for MPSC test");

    constexpr int ProducerCount = 4;
    constexpr int CommandsPerProducer = 64;
    ve::AtomicInt32 counter{0};
    std::array<ve::Thread, ProducerCount> producers;

    for (int producerIndex = 0; producerIndex < ProducerCount; ++producerIndex)
    {
        passed &= ExpectOk(
            producers[producerIndex].Start(ve::ThreadDesc{"RenderCommandProducer"}, [&]()
            {
                for (int commandIndex = 0; commandIndex < CommandsPerProducer; ++commandIndex)
                {
                    ve::RenderCommand command;
                    command.debugName = "MpscIncrement";
                    command.function = [&](ve::RenderThreadContext&)
                    {
                        counter.fetch_add(1, std::memory_order_acq_rel);
                    };

                    const ve::Result<void> submitResult = renderSystem.Submit(std::move(command));
                    if (!submitResult)
                    {
                        counter.fetch_add(-100000, std::memory_order_acq_rel);
                    }
                }
            }),
            "Producer thread should start");
    }

    for (ve::Thread& producer : producers)
    {
        passed &= Expect(producer.Join(), "Producer thread should join");
    }

    passed &= ExpectOk(renderSystem.Flush(), "Flush should wait for MPSC commands");
    passed &= Expect(
        counter.load(std::memory_order_acquire) == ProducerCount * CommandsPerProducer,
        "All producer-submitted commands should execute exactly once");

    renderSystem.Shutdown();
    return passed;
}
} // namespace

int main()
{
    bool passed = true;

    passed &= TestInitializeAndShutdown();
    passed &= TestRepeatedInitializeWhileRunningFails();
    passed &= TestInitializeAfterShutdownSucceeds();
    passed &= TestSubmitBeforeInitializeAndAfterShutdownFails();
    passed &= TestEmptyCommandFails();
    passed &= TestCommandExecutesOnRenderThread();
    passed &= TestFlushWaitsForAcceptedCommands();
    passed &= TestShutdownDrainsAcceptedCommands();
    passed &= TestMultipleProducerThreadsSubmitCommands();

    if (passed)
    {
        std::cout << "VEngineRenderSystemTests passed" << '\n';
        return 0;
    }

    return 1;
}
