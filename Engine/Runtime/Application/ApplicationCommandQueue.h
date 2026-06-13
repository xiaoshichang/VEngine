#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"

#include <functional>
#include <mutex>
#include <vector>

namespace ve
{
    using ApplicationCommand = std::function<void()>;

    class ApplicationCommandQueue : public NonMovable
    {
    public:
        ApplicationCommandQueue() = default;

        void Enqueue(ApplicationCommand command);
        void ExecutePending();
        void Clear();

    private:
        std::mutex mutex_;
        std::vector<ApplicationCommand> pendingCommands_;
        std::vector<ApplicationCommand> executingCommands_;
    };
} // namespace ve
