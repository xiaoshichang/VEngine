#include "Engine/Runtime/IO/IOSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/FileSystem/FileSystem.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <new>
#include <utility>
#include <vector>

namespace ve
{
    namespace
    {
        enum class IORequestStatus
        {
            Pending,
            Running,
            Complete,
        };
    } // namespace

    struct IORequestState
    {
        explicit IORequestState(IOReadRequestDesc requestDesc)
            : desc(std::move(requestDesc))
        {
        }

        IOReadRequestDesc desc;
        IORequestStatus status = IORequestStatus::Pending;
        std::vector<std::byte> data;
        Error error;

        mutable std::mutex completionMutex;
        mutable std::condition_variable completionCondition;
        bool complete = false;
    };

    struct IOSystemImpl
    {
        Thread thread;
        std::deque<std::shared_ptr<IORequestState>> highPriorityQueue;
        std::deque<std::shared_ptr<IORequestState>> normalPriorityQueue;
        std::deque<std::shared_ptr<IORequestState>> completedReads;
        mutable std::mutex mutex;
        std::condition_variable condition;
        bool initialized = false;
        bool acceptingRequests = false;
        bool stopping = false;
        SizeT incompleteRequestCount = 0;
    };

    namespace
    {
        [[nodiscard]] bool HasPendingRequest(const IOSystemImpl& impl)
        {
            return !impl.highPriorityQueue.empty() || !impl.normalPriorityQueue.empty();
        }

        void EnqueuePendingRequest(IOSystemImpl& impl, const std::shared_ptr<IORequestState>& state)
        {
            if (state->desc.priority == IORequestPriority::High)
            {
                impl.highPriorityQueue.push_back(state);
            }
            else
            {
                impl.normalPriorityQueue.push_back(state);
            }
        }

        [[nodiscard]] std::shared_ptr<IORequestState> PopPendingRequest(IOSystemImpl& impl)
        {
            if (!impl.highPriorityQueue.empty())
            {
                std::shared_ptr<IORequestState> state = std::move(impl.highPriorityQueue.front());
                impl.highPriorityQueue.pop_front();
                return state;
            }

            if (!impl.normalPriorityQueue.empty())
            {
                std::shared_ptr<IORequestState> state = std::move(impl.normalPriorityQueue.front());
                impl.normalPriorityQueue.pop_front();
                return state;
            }

            return {};
        }

        void MarkRequestRunning(const std::shared_ptr<IORequestState>& state)
        {
            std::lock_guard<std::mutex> lock(state->completionMutex);
            state->status = IORequestStatus::Running;
        }

        void StoreReadResult(IOSystemImpl& impl,
                             const std::shared_ptr<IORequestState>& state,
                             Result<std::vector<std::byte>> readResult)
        {
            {
                std::lock_guard<std::mutex> completionLock(state->completionMutex);

                if (readResult)
                {
                    state->data = readResult.MoveValue();
                    state->error = Error();
                }
                else
                {
                    state->data.clear();
                    state->error = readResult.GetError();
                }

                state->status = IORequestStatus::Complete;
            }

            {
                std::lock_guard<std::mutex> lock(impl.mutex);
                impl.completedReads.push_back(state);

                VE_ASSERT_MESSAGE(impl.incompleteRequestCount > 0, "IOSystem incomplete request count underflowed.");
                --impl.incompleteRequestCount;
            }

            impl.condition.notify_all();

            {
                std::lock_guard<std::mutex> completionLock(state->completionMutex);
                state->complete = true;
            }
            state->completionCondition.notify_all();
        }

        void IOThreadLoop(IOSystemImpl& impl)
        {
            for (;;)
            {
                std::shared_ptr<IORequestState> state;

                {
                    std::unique_lock<std::mutex> lock(impl.mutex);
                    impl.condition.wait(lock, [&]() { return impl.stopping || HasPendingRequest(impl); });

                    if (!HasPendingRequest(impl))
                    {
                        if (impl.stopping)
                        {
                            return;
                        }

                        continue;
                    }

                    state = PopPendingRequest(impl);
                }

                if (state)
                {
                    MarkRequestRunning(state);
                    StoreReadResult(impl, state, FileSystem::ReadBinaryFile(state->desc.path));
                }
            }
        }

        void StopAndJoinIOThread(IOSystemImpl& impl) noexcept
        {
            {
                std::lock_guard<std::mutex> lock(impl.mutex);
                impl.acceptingRequests = false;
                impl.stopping = true;
            }
            impl.condition.notify_all();

            if (impl.thread.IsJoinable())
            {
                static_cast<void>(impl.thread.Join());
            }

            impl.highPriorityQueue.clear();
            impl.normalPriorityQueue.clear();
            impl.completedReads.clear();
        }
    } // namespace

    IORequestHandle::IORequestHandle(std::shared_ptr<IORequestState> state)
        : state_(std::move(state))
    {
    }

    bool IORequestHandle::IsValid() const noexcept
    {
        return state_ != nullptr;
    }

    void IORequestHandle::Reset() noexcept
    {
        state_.reset();
    }

    bool IOReadResult::IsOk() const noexcept
    {
        return error.IsOk();
    }

    IOSystem::IOSystem()
        : impl_(std::make_unique<IOSystemImpl>())
    {
    }

    IOSystem::~IOSystem()
    {
        Shutdown();
    }

    ErrorCode IOSystem::Initialize(const IOSystemDesc& desc)
    {
        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (impl_->initialized)
            {
                return ErrorCode::InvalidState;
            }

            impl_->initialized = true;
            impl_->acceptingRequests = true;
            impl_->stopping = false;
            impl_->incompleteRequestCount = 0;
        }

        ErrorCode startResult =
            impl_->thread.Start(ThreadDesc{desc.threadName}, [impl = impl_.get()]() { IOThreadLoop(*impl); });

        if (startResult != ErrorCode::None)
        {
            Shutdown();
            return startResult;
        }

        return ErrorCode::None;
    }

    void IOSystem::Shutdown() noexcept
    {
        {
            std::unique_lock<std::mutex> lock(impl_->mutex);
            if (!impl_->initialized)
            {
                return;
            }

            impl_->acceptingRequests = false;
            impl_->condition.wait(lock, [&]() { return impl_->incompleteRequestCount == 0; });
        }

        StopAndJoinIOThread(*impl_);

        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            impl_->initialized = false;
            impl_->stopping = false;
        }
    }

    bool IOSystem::IsInitialized() const noexcept
    {
        std::lock_guard<std::mutex> lock(impl_->mutex);
        return impl_->initialized;
    }

    Result<IORequestHandle> IOSystem::ReadBinaryFile(const IOReadRequestDesc& desc)
    {
        if (desc.path.IsEmpty())
        {
            return Result<IORequestHandle>::Failure(
                Error(ErrorCode::InvalidArgument, "IOSystem read request requires a non-empty path."));
        }

        std::shared_ptr<IORequestState> state;
        try
        {
            state = std::make_shared<IORequestState>(desc);
        }
        catch (const std::bad_alloc&)
        {
            return Result<IORequestHandle>::Failure(Error(ErrorCode::OutOfMemory, "IO request allocation failed."));
        }

        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (!impl_->initialized || !impl_->acceptingRequests)
            {
                return Result<IORequestHandle>::Failure(
                    Error(ErrorCode::InvalidState, "IOSystem is not accepting requests."));
            }

            ++impl_->incompleteRequestCount;
            EnqueuePendingRequest(*impl_, state);
        }

        impl_->condition.notify_one();
        return Result<IORequestHandle>::Success(IORequestHandle(std::move(state)));
    }

    bool IOSystem::IsComplete(const IORequestHandle& handle) const noexcept
    {
        if (!handle.IsValid())
        {
            return true;
        }

        std::lock_guard<std::mutex> lock(handle.state_->completionMutex);
        return handle.state_->complete;
    }

    void IOSystem::Wait(const IORequestHandle& handle) const
    {
        if (!handle.IsValid())
        {
            return;
        }

        std::unique_lock<std::mutex> lock(handle.state_->completionMutex);
        handle.state_->completionCondition.wait(lock, [&]() { return handle.state_->complete; });
    }

    void IOSystem::WaitAll(std::span<const IORequestHandle> handles) const
    {
        for (const IORequestHandle& handle : handles)
        {
            Wait(handle);
        }
    }

    bool IOSystem::TryPopCompletedRead(IOReadResult& outResult)
    {
        std::shared_ptr<IORequestState> state;

        {
            std::lock_guard<std::mutex> lock(impl_->mutex);
            if (impl_->completedReads.empty())
            {
                return false;
            }

            state = std::move(impl_->completedReads.front());
            impl_->completedReads.pop_front();
        }

        {
            std::lock_guard<std::mutex> completionLock(state->completionMutex);
            outResult.handle = IORequestHandle(state);
            outResult.path = state->desc.path;
            outResult.data = std::move(state->data);
            outResult.error = std::move(state->error);
        }

        return true;
    }
} // namespace ve
