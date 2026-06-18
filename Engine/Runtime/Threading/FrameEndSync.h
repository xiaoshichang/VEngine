#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"
#include "ThreadEnsure.h"

#include <utility>

namespace ve
{
    class FrameEndFence : public NonCopyable
    {
    public:
        FrameEndFence()
            : event_(true)
        {
        }

        void BeginFence()
        {
            event_.Reset();
        }

        void Emit()
        {
            event_.Set();
        }

        void Wait()
        {
            event_.Wait();
        }

        void Reset()
        {
            event_.Set();
        }

    private:
        ManualResetEvent event_;
    };

    /// Limits Main Thread lead over Scene Thread to at most one frame.
    ///
    /// Uses two alternating frame-end fences (ping-pong), similar to Unreal-style frame-end synchronization.
    class MainThreadSceneThreadFrameEndSync : public NonCopyable
    {
    public:
        template<typename EnqueueFenceSignalFunction>
        void NotifyMainThreadFrameEnd(EnqueueFenceSignalFunction&& enqueueFenceSignal)
        {
            const UInt32 signalFenceIndex = mainThreadFenceIndex_;
            FrameEndFence& fence = sceneThreadFrameEndFences_[mainThreadFenceIndex_];
            fence.BeginFence();

            mainThreadFenceIndex_ = (mainThreadFenceIndex_ + 1) & 1u;

            std::forward<EnqueueFenceSignalFunction>(enqueueFenceSignal)(signalFenceIndex);
            sceneThreadFrameEndFences_[mainThreadFenceIndex_].Wait();
        }

        void NotifySceneThreadFrameEnd(UInt32 fenceIndex)
        {
            VE_ASSERT_SCENE_THREAD();
            sceneThreadFrameEndFences_[fenceIndex & 1u].Emit();
        }

        void UnblockAllWaiters()
        {
            sceneThreadFrameEndFences_[0].Emit();
            sceneThreadFrameEndFences_[1].Emit();
        }

        void Reset()
        {
            mainThreadFenceIndex_ = 0;
            sceneThreadFrameEndFences_[0].Reset();
            sceneThreadFrameEndFences_[1].Reset();
        }

    private:
        UInt32 mainThreadFenceIndex_ = 0;
        FrameEndFence sceneThreadFrameEndFences_[2] = {
            FrameEndFence(),
            FrameEndFence(),
        };
    };

    /// Limits Scene Thread lead over Render Thread to at most one frame.
    ///
    /// Uses two alternating frame-end fences (ping-pong), similar to Unreal-style frame-end synchronization.
    class SceneThreadRenderThreadFrameEndSync : public NonCopyable
    {
    public:
        template<typename EnqueueFenceSignalFunction>
        void NotifySceneThreadFrameEndAndWait(const AtomicBool& stopRequested,
                                              EnqueueFenceSignalFunction&& enqueueFenceSignal)
        {
            if (stopRequested.load(std::memory_order_acquire))
            {
                return;
            }

            const UInt32 signalFenceIndex = sceneThreadFenceIndex_;
            FrameEndFence& fence = renderThreadFrameEndFences_[sceneThreadFenceIndex_];
            fence.BeginFence();

            sceneThreadFenceIndex_ = (sceneThreadFenceIndex_ + 1) & 1u;

            std::forward<EnqueueFenceSignalFunction>(enqueueFenceSignal)(signalFenceIndex);

            // Shutdown path should call UnblockAllWaiters() to release this wait.
            renderThreadFrameEndFences_[sceneThreadFenceIndex_].Wait();
        }

        void NotifyRenderThreadFrameEnd(UInt32 fenceIndex)
        {
            renderThreadFrameEndFences_[fenceIndex & 1u].Emit();
        }

        void UnblockAllWaiters()
        {
            renderThreadFrameEndFences_[0].Emit();
            renderThreadFrameEndFences_[1].Emit();
        }

        void Reset()
        {
            sceneThreadFenceIndex_ = 0;
            renderThreadFrameEndFences_[0].Reset();
            renderThreadFrameEndFences_[1].Reset();
        }

    private:
        UInt32 sceneThreadFenceIndex_ = 0;
        FrameEndFence renderThreadFrameEndFences_[2] = {
            FrameEndFence(),
            FrameEndFence(),
        };
    };
} // namespace ve
