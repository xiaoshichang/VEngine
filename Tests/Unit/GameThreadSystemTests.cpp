#include "Engine/Runtime/Application/EngineRuntime.h"
#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/GameThread/GameThreadSystem.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <chrono>
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

bool ExpectOk(ve::ErrorCode result, const char* message)
{
    if (result == ve::ErrorCode::None)
    {
        return true;
    }

    std::cerr << "FAILED: " << message << ": " << ve::ToString(result) << '\n';
    return false;
}

ve::GameThreadSystemDesc MakeGameThreadSystemDesc()
{
    ve::GameThreadSystemDesc desc;
    desc.threadName = "GameThreadSystemTestThread";
    return desc;
}

ve::EngineRuntimeDesc MakeRuntimeDesc()
{
    ve::EngineRuntimeDesc desc;
    desc.jobSystem.workerThreadCount = 1;
    desc.jobSystem.workerThreadNamePrefix = "GameThreadRuntimeTestJobWorker";
    desc.ioSystem.threadName = "GameThreadRuntimeTestIOThread";
    desc.renderSystem.threadName = "GameThreadRuntimeTestRenderThread";
    desc.gameThreadSystem.threadName = "GameThreadRuntimeTestGameThread";
    return desc;
}

bool WaitForCompletedFrameCount(const ve::GameThreadSystem& gameThreadSystem, ve::UInt64 minimumFrameCount)
{
    for (int attempt = 0; attempt < 1000; ++attempt)
    {
        if (gameThreadSystem.GetCompletedFrameCount() >= minimumFrameCount)
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

    ve::GameThreadSystem gameThreadSystem;
    passed &= Expect(!gameThreadSystem.IsInitialized(), "New GameThreadSystem should not be initialized");
    passed &= Expect(
        gameThreadSystem.GetCurrentPhase() == ve::GameThreadPhase::Uninitialized,
        "New GameThreadSystem should start with Uninitialized phase");
    passed &= ExpectOk(gameThreadSystem.Initialize(MakeGameThreadSystemDesc()), "GameThreadSystem should initialize");
    passed &= Expect(gameThreadSystem.IsInitialized(), "Initialized GameThreadSystem should report initialized");
    passed &= Expect(
        gameThreadSystem.GetGameThreadId().IsValid(),
        "Initialized GameThreadSystem should expose a Game Thread id");
    passed &= Expect(
        gameThreadSystem.GetGameThreadId() != ve::GetCurrentThreadId(),
        "GameThreadSystem should own a dedicated physical Game Thread");

    gameThreadSystem.Shutdown();
    passed &= Expect(!gameThreadSystem.IsInitialized(), "Shutdown GameThreadSystem should report uninitialized");
    passed &= Expect(
        gameThreadSystem.GetCurrentPhase() == ve::GameThreadPhase::Uninitialized,
        "Shutdown GameThreadSystem should return to Uninitialized phase");

    gameThreadSystem.Shutdown();
    passed &= Expect(!gameThreadSystem.IsInitialized(), "Repeated Shutdown should remain a no-op");

    return passed;
}

bool TestRepeatedInitializeWhileRunningFails()
{
    bool passed = true;

    ve::GameThreadSystem gameThreadSystem;
    passed &= ExpectOk(
        gameThreadSystem.Initialize(MakeGameThreadSystemDesc()),
        "Initial GameThreadSystem Initialize should succeed");

    const ve::ErrorCode repeatedInitialize = gameThreadSystem.Initialize(MakeGameThreadSystemDesc());
    passed &= Expect(
        repeatedInitialize != ve::ErrorCode::None,
        "Repeated GameThreadSystem Initialize while running should fail");
    passed &= Expect(
        repeatedInitialize == ve::ErrorCode::InvalidState,
        "Repeated GameThreadSystem Initialize should report InvalidState");

    gameThreadSystem.Shutdown();
    return passed;
}

bool TestInitializeAfterShutdownSucceeds()
{
    bool passed = true;

    ve::GameThreadSystem gameThreadSystem;
    passed &= ExpectOk(
        gameThreadSystem.Initialize(MakeGameThreadSystemDesc()),
        "Initial GameThreadSystem lifecycle should initialize");
    gameThreadSystem.Shutdown();
    passed &= ExpectOk(
        gameThreadSystem.Initialize(MakeGameThreadSystemDesc()),
        "Standalone GameThreadSystem should initialize after Shutdown");
    gameThreadSystem.Shutdown();

    return passed;
}

bool TestGameThreadOwnsFrameLoop()
{
    bool passed = true;

    ve::GameThreadSystem gameThreadSystem;
    passed &= ExpectOk(
        gameThreadSystem.Initialize(MakeGameThreadSystemDesc()),
        "GameThreadSystem should initialize for frame loop test");
    passed &= Expect(
        WaitForCompletedFrameCount(gameThreadSystem, 2),
        "GameThreadSystem should advance frames from its own loop");
    passed &= Expect(
        gameThreadSystem.GetFrameIndex() >= gameThreadSystem.GetCompletedFrameCount(),
        "Frame index should not trail completed frame count");
    passed &= Expect(
        gameThreadSystem.GetCurrentPhase() != ve::GameThreadPhase::Uninitialized,
        "Running GameThreadSystem should leave Uninitialized phase");

    gameThreadSystem.Shutdown();
    return passed;
}

bool TestGameThreadAccessRule()
{
    bool passed = true;

    ve::GameThreadSystem gameThreadSystem;
    passed &= ExpectOk(
        gameThreadSystem.Initialize(MakeGameThreadSystemDesc()),
        "GameThreadSystem should initialize for access test");

    passed &= Expect(
        !gameThreadSystem.CheckGameThreadAccess(),
        "Calling thread should not be the dedicated Game Thread");
    passed &= Expect(
        gameThreadSystem.GetGameThreadId() != ve::GetCurrentThreadId(),
        "Game Thread id should differ from the calling thread id");

    gameThreadSystem.Shutdown();
    return passed;
}

bool TestEngineRuntimeOwnsGameThreadSystem()
{
    bool passed = true;

    ve::EngineRuntime runtime;
    passed &= ExpectOk(
        runtime.Initialize(MakeRuntimeDesc()),
        "EngineRuntime should initialize for GameThreadSystem test");
    passed &= Expect(
        runtime.GetGameThreadSystem().IsInitialized(),
        "Runtime-owned GameThreadSystem should be initialized");
    passed &= Expect(
        runtime.GetGameThreadSystem().GetGameThreadId().IsValid(),
        "Runtime-owned GameThreadSystem should expose a Game Thread id");
    passed &= Expect(
        WaitForCompletedFrameCount(runtime.GetGameThreadSystem(), 2),
        "Runtime-owned GameThreadSystem should advance frames from its own loop");

    runtime.Shutdown();
    return passed;
}
} // namespace

int main()
{
    bool passed = true;

    passed &= TestInitializeAndShutdown();
    passed &= TestRepeatedInitializeWhileRunningFails();
    passed &= TestInitializeAfterShutdownSucceeds();
    passed &= TestGameThreadOwnsFrameLoop();
    passed &= TestGameThreadAccessRule();
    passed &= TestEngineRuntimeOwnsGameThreadSystem();

    if (passed)
    {
        std::cout << "VEngineGameThreadSystemTests passed" << '\n';
        return 0;
    }

    return 1;
}
