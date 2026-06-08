#include "Engine/Runtime/Scene/SceneSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Logging/Log.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"

#include <chrono>
#include <exception>
#include <new>

namespace ve
{
struct SceneSystemImpl
{
    Thread thread;
    RecursiveMutex sceneMutex;
    std::unique_ptr<Scene> scene;
    Atomic<UInt64> sceneThreadIdValue{0};
    AtomicBool initialized{false};
    AtomicBool stopRequested{false};
};

namespace
{
constexpr std::chrono::milliseconds SceneThreadIdleSleep(1);

void UpdateScene(SceneSystemImpl& impl, Float32 deltaSeconds)
{
    LockGuard<RecursiveMutex> lock(impl.sceneMutex);
    if (impl.scene != nullptr)
    {
        impl.scene->Update(deltaSeconds);
    }
}

void SceneThreadLoop(SceneSystemImpl& impl)
{
    using Clock = std::chrono::steady_clock;

    impl.sceneThreadIdValue.store(GetCurrentThreadId().value, std::memory_order_release);

    Clock::time_point lastUpdateTime = Clock::now();
    while (!impl.stopRequested.load(std::memory_order_acquire))
    {
        const Clock::time_point now = Clock::now();
        const std::chrono::duration<Float32> rawDelta = now - lastUpdateTime;
        lastUpdateTime = now;

        try
        {
            UpdateScene(impl, rawDelta.count());
        }
        catch (...)
        {
            VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped SceneSystem update.");
        }

        if (!impl.stopRequested.load(std::memory_order_acquire))
        {
            SleepFor(SceneThreadIdleSleep);
        }
    }

    impl.sceneThreadIdValue.store(0, std::memory_order_release);
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
}
}

SceneSystem::SceneSystem()
    : impl_(std::make_unique<SceneSystemImpl>())
{
}

SceneSystem::~SceneSystem()
{
    Shutdown();
}

ErrorCode SceneSystem::Initialize(const SceneSystemInitParam& initParam)
{
    if (impl_->initialized.load(std::memory_order_acquire))
    {
        return ErrorCode::InvalidState;
    }

    {
        LockGuard<RecursiveMutex> lock(impl_->sceneMutex);
        if (impl_->scene == nullptr)
        {
            try
            {
                impl_->scene = std::make_unique<Scene>();
            }
            catch (const std::bad_alloc&)
            {
                return ErrorCode::OutOfMemory;
            }
        }
    }

    impl_->stopRequested.store(false, std::memory_order_release);

    ErrorCode startResult = impl_->thread.Start(initParam.threadName.empty() ? ThreadDesc{"VEngineSceneThread"}
                                                                            : ThreadDesc{initParam.threadName},
        [this]()
        {
            SceneThreadLoop(*impl_);
        });

    if (startResult != ErrorCode::None)
    {
        impl_->stopRequested.store(false, std::memory_order_release);
        return startResult;
    }

    impl_->initialized.store(true, std::memory_order_release);
    VE_LOG_INFO("SceneSystem initialized.");
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
    LockGuard<RecursiveMutex> lock(impl_->sceneMutex);
    return impl_->scene.get();
}

const Scene* SceneSystem::GetScene() const noexcept
{
    LockGuard<RecursiveMutex> lock(impl_->sceneMutex);
    return impl_->scene.get();
}

void SceneSystem::ReplaceScene(std::unique_ptr<Scene> scene)
{
    LockGuard<RecursiveMutex> lock(impl_->sceneMutex);
    impl_->scene = scene != nullptr ? std::move(scene) : std::make_unique<Scene>();
}

void SceneSystem::Update(Float32 deltaSeconds)
{
    UpdateScene(*impl_, deltaSeconds);
}
}
