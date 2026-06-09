#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Types.h"
#include "Engine/Runtime/Threading/LockFreeSpscQueue.h"

#include <utility>

namespace ve
{
    enum class OSEventType
    {
        WindowFocusGained,
        WindowFocusLost,
        WindowMinimized,
        WindowRestored,
        WindowResized,
        WindowShown,
        WindowHidden,
        FrameEndFenceSignal,
    };

    struct OSEvent
    {
        OSEventType type = OSEventType::WindowShown;
        UInt32 width = 0;
        UInt32 height = 0;
        UInt32 fenceIndex = 0;
    };

    /// Single-producer single-consumer OS event queue for main-thread to scene-thread dispatch.
    class OSEventQueue : public NonMovable
    {
    public:
        [[nodiscard]] ErrorCode Push(OSEvent event)
        {
            if (!queue_.TryPush(std::move(event)))
            {
                return ErrorCode::InvalidState;
            }

            return ErrorCode::None;
        }

        [[nodiscard]] bool TryPop(OSEvent& outEvent)
        {
            return queue_.TryPop(outEvent);
        }

        void ClearForConsumer()
        {
            OSEvent event;
            while (queue_.TryPop(event))
            {
            }
        }

    private:
        static constexpr SizeT QueueCapacity = 256;

        LockFreeSpscQueue<OSEvent, QueueCapacity> queue_;
    };
} // namespace ve
