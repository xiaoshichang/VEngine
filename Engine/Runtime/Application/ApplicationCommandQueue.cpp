#include "Engine/Runtime/Application/ApplicationCommandQueue.h"

#include <utility>

namespace ve
{
    void ApplicationCommandQueue::Enqueue(ApplicationCommand command)
    {
        if (command == nullptr)
        {
            return;
        }

        std::lock_guard lock(mutex_);
        pendingCommands_.push_back(std::move(command));
    }

    void ApplicationCommandQueue::ExecutePending()
    {
        {
            std::lock_guard lock(mutex_);
            if (pendingCommands_.empty())
            {
                return;
            }

            executingCommands_.swap(pendingCommands_);
        }

        for (ApplicationCommand& command : executingCommands_)
        {
            command();
        }
        executingCommands_.clear();
    }

    void ApplicationCommandQueue::Clear()
    {
        std::lock_guard lock(mutex_);
        pendingCommands_.clear();
        executingCommands_.clear();
    }
} // namespace ve
