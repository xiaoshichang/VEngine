#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/IO/IOSystem.h"
#include "Engine/Runtime/Render/RenderSystem.h"
#include "Engine/Runtime/Threading/Atomic.h"

#include <filesystem>
#include <iostream>
#include <system_error>

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

bool ExpectJobHandleResult(const ve::Result<ve::JobHandle>& result, const char* message)
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

template <typename T>
bool ExpectResultOk(const ve::Result<T>& result, const char* message)
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

void RemoveTestRoot()
{
    std::error_code error;
    std::filesystem::remove_all(std::filesystem::path("Generated") / "EngineRuntimeTests", error);
}

ve::EngineRuntimeDesc MakeRuntimeDesc()
{
    ve::EngineRuntimeDesc desc;
    desc.jobSystem.workerThreadCount = 1;
    desc.jobSystem.workerThreadNamePrefix = "EngineRuntimeTestJobWorker";
    desc.ioSystem.threadName = "EngineRuntimeTestIOThread";
    desc.renderSystem.threadName = "EngineRuntimeTestRenderThread";
    return desc;
}

bool TestInitializeAndShutdown()
{
    bool passed = true;

    ve::EngineRuntime runtime;
    passed &= Expect(!runtime.IsInitialized(), "New runtime should not be initialized");
    passed &= Expect(!runtime.HasInitialized(), "New runtime should not report a completed lifecycle");

    passed &= ExpectOk(runtime.Initialize(MakeRuntimeDesc()), "EngineRuntime should initialize");
    passed &= Expect(runtime.IsInitialized(), "Initialized runtime should report initialized");
    passed &= Expect(runtime.HasInitialized(), "Initialized runtime should report completed lifecycle");
    passed &= Expect(runtime.GetJobSystem().IsInitialized(), "Runtime-owned JobSystem should be initialized");
    passed &= Expect(runtime.GetIOSystem().IsInitialized(), "Runtime-owned IOSystem should be initialized");
    passed &= Expect(runtime.GetRenderSystem().IsInitialized(), "Runtime-owned RenderSystem should be initialized");

    runtime.Shutdown();
    passed &= Expect(!runtime.IsInitialized(), "Shutdown runtime should report uninitialized");
    passed &= Expect(runtime.HasInitialized(), "Shutdown runtime should remember completed lifecycle");

    runtime.Shutdown();
    passed &= Expect(!runtime.IsInitialized(), "Repeated Shutdown should remain a no-op");

    return passed;
}

bool TestRepeatedInitializeWhileRunningFails()
{
    bool passed = true;

    ve::EngineRuntime runtime;
    passed &= ExpectOk(runtime.Initialize(MakeRuntimeDesc()), "Initial EngineRuntime Initialize should succeed");

    const ve::Result<void> repeatedInitialize = runtime.Initialize(MakeRuntimeDesc());
    passed &= Expect(!repeatedInitialize, "Repeated Initialize while running should fail");
    if (!repeatedInitialize)
    {
        passed &= Expect(
            repeatedInitialize.GetError().GetCode() == ve::ErrorCode::InvalidState,
            "Repeated Initialize while running should report InvalidState");
    }

    runtime.Shutdown();
    return passed;
}

bool TestRepeatedLifecycleFails()
{
    bool passed = true;

    ve::EngineRuntime runtime;
    passed &= ExpectOk(runtime.Initialize(MakeRuntimeDesc()), "Initial EngineRuntime lifecycle should initialize");
    runtime.Shutdown();

    const ve::Result<void> secondLifecycle = runtime.Initialize(MakeRuntimeDesc());
    passed &= Expect(!secondLifecycle, "EngineRuntime should reject Initialize after Shutdown");
    if (!secondLifecycle)
    {
        passed &= Expect(
            secondLifecycle.GetError().GetCode() == ve::ErrorCode::InvalidState,
            "EngineRuntime second lifecycle should report InvalidState");
    }

    return passed;
}

bool TestCanScheduleJobThroughRuntime()
{
    bool passed = true;

    ve::EngineRuntime runtime;
    passed &= ExpectOk(runtime.Initialize(MakeRuntimeDesc()), "EngineRuntime should initialize for job access test");

    ve::AtomicInt32 value{0};
    ve::Result<ve::JobHandle> handleResult = runtime.GetJobSystem().Schedule(ve::JobDesc{"RuntimeJob"}, [&]()
    {
        value.store(17, std::memory_order_release);
    });

    passed &= ExpectJobHandleResult(handleResult, "Runtime-owned JobSystem should schedule jobs");
    if (handleResult)
    {
        ve::JobHandle handle = handleResult.MoveValue();
        runtime.GetJobSystem().Wait(handle);
    }

    passed &= Expect(value.load(std::memory_order_acquire) == 17, "Job scheduled through runtime should execute");

    runtime.Shutdown();
    return passed;
}

bool TestCanReadThroughRuntimeIOSystem()
{
    bool passed = true;

    const ve::Path path = ve::FileSystem::GetCurrentWorkingDirectory() / "Generated/EngineRuntimeTests/IOSystem.bin";
    passed &= ExpectOk(ve::FileSystem::WriteTextFile(path, "runtime io"), "Runtime IO test file should be written");

    ve::EngineRuntime runtime;
    passed &= ExpectOk(runtime.Initialize(MakeRuntimeDesc()), "EngineRuntime should initialize for IO access test");

    ve::IOReadRequestDesc desc;
    desc.path = path;
    desc.debugName = "RuntimeIOSystemRead";

    ve::Result<ve::IORequestHandle> request = runtime.GetIOSystem().ReadBinaryFile(desc);
    passed &= ExpectResultOk(request, "Runtime-owned IOSystem should schedule reads");

    if (request)
    {
        runtime.GetIOSystem().Wait(request.GetValue());
    }

    ve::IOReadResult result;
    passed &= Expect(runtime.GetIOSystem().TryPopCompletedRead(result), "Runtime-owned IOSystem should return completion");
    passed &= Expect(result.IsOk(), "Runtime-owned IOSystem read should succeed");

    runtime.Shutdown();
    return passed;
}

bool TestCanSubmitThroughRuntimeRenderSystem()
{
    bool passed = true;

    ve::EngineRuntime runtime;
    passed &= ExpectOk(runtime.Initialize(MakeRuntimeDesc()), "EngineRuntime should initialize for render access test");

    ve::AtomicInt32 value{0};
    ve::ThreadId executedThreadId;

    ve::RenderCommand command;
    command.debugName = "RuntimeRenderCommand";
    command.function = [&](ve::RenderThreadContext& context)
    {
        executedThreadId = context.GetRenderThreadId();
        value.store(29, std::memory_order_release);
    };

    passed &= ExpectOk(runtime.GetRenderSystem().Submit(std::move(command)), "Runtime-owned RenderSystem should accept commands");
    passed &= ExpectOk(runtime.GetRenderSystem().Flush(), "Runtime-owned RenderSystem should flush commands");
    passed &= Expect(value.load(std::memory_order_acquire) == 29, "Runtime-owned RenderSystem command should execute");
    passed &= Expect(executedThreadId.IsValid(), "Runtime-owned RenderSystem command should see a Render Thread id");

    runtime.Shutdown();
    return passed;
}
} // namespace

int main()
{
    RemoveTestRoot();

    bool passed = true;

    passed &= TestInitializeAndShutdown();
    passed &= TestRepeatedInitializeWhileRunningFails();
    passed &= TestRepeatedLifecycleFails();
    passed &= TestCanScheduleJobThroughRuntime();
    passed &= TestCanReadThroughRuntimeIOSystem();
    passed &= TestCanSubmitThroughRuntimeRenderSystem();

    RemoveTestRoot();

    if (passed)
    {
        std::cout << "VEngineRuntimeTests passed" << '\n';
        return 0;
    }

    return 1;
}
