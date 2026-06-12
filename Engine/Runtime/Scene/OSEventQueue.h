#pragma once

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Input/OSEvent.h"
#include "Engine/Runtime/Threading/LockFreeSpscQueue.h"

#include <utility>

namespace ve
{
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
