#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Threading/Atomic.h"

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

ve::EngineRuntimeDesc MakeRuntimeDesc()
{
    ve::EngineRuntimeDesc desc;
    desc.jobSystem.workerThreadCount = 1;
    desc.jobSystem.workerThreadNamePrefix = "EngineRuntimeTestJobWorker";
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
} // namespace

int main()
{
    bool passed = true;

    passed &= TestInitializeAndShutdown();
    passed &= TestRepeatedInitializeWhileRunningFails();
    passed &= TestRepeatedLifecycleFails();
    passed &= TestCanScheduleJobThroughRuntime();

    if (passed)
    {
        std::cout << "VEngineRuntimeTests passed" << '\n';
        return 0;
    }

    return 1;
}
