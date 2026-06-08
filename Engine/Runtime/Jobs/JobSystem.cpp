#include "Engine/Runtime/Jobs/JobSystem.h"

#include "Engine/Runtime/Core/Assert.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace ve
{
    struct JobState
    {
        explicit JobState(JobDesc jobDesc, std::function<void()> jobFunction)
            : desc(std::move(jobDesc))
            , function(std::move(jobFunction))
        {
        }

        JobDesc desc;
        std::function<void()> function;
        std::vector<std::shared_ptr<JobState>> continuations;
        SizeT pendingDependencies = 0;
        bool schedulerComplete = false;

        mutable std::mutex completionMutex;
        mutable std::condition_variable completionCondition;
        bool complete = false;
    };

    struct JobSystemImpl
    {
        std::vector<std::unique_ptr<Thread>> workers;
        std::deque<std::shared_ptr<JobState>> highPriorityQueue;
        std::deque<std::shared_ptr<JobState>> normalPriorityQueue;
        mutable std::mutex schedulerMutex;
        std::condition_variable schedulerCondition;
        std::condition_variable allJobsCompleteCondition;
        SizeT incompleteJobCount = 0;
        SizeT workerThreadCount = 0;
        bool initialized = false;
        bool acceptingJobs = false;
        bool stopping = false;
    };

    namespace
    {
        [[nodiscard]] SizeT ResolveWorkerThreadCount(SizeT requestedCount)
        {
            if (requestedCount != 0)
            {
                return requestedCount;
            }

            const unsigned int hardwareThreads = std::thread::hardware_concurrency();
            if (hardwareThreads <= 1)
            {
                return 1;
            }

            return static_cast<SizeT>(hardwareThreads - 1);
        }

        [[nodiscard]] bool HasRunnableJob(const JobSystemImpl& impl)
        {
            return !impl.highPriorityQueue.empty() || !impl.normalPriorityQueue.empty();
        }

        void EnqueueReadyJob(JobSystemImpl& impl, const std::shared_ptr<JobState>& state)
        {
            if (state->desc.priority == JobPriority::High)
            {
                impl.highPriorityQueue.push_back(state);
            }
            else
            {
                impl.normalPriorityQueue.push_back(state);
            }
        }

        void RemoveContinuation(JobState& dependency, const std::shared_ptr<JobState>& continuation)
        {
            const auto iterator =
                std::find(dependency.continuations.begin(), dependency.continuations.end(), continuation);
            if (iterator != dependency.continuations.end())
            {
                dependency.continuations.erase(iterator);
            }
        }

        [[nodiscard]] std::shared_ptr<JobState> PopReadyJob(JobSystemImpl& impl)
        {
            if (!impl.highPriorityQueue.empty())
            {
                std::shared_ptr<JobState> state = std::move(impl.highPriorityQueue.front());
                impl.highPriorityQueue.pop_front();
                return state;
            }

            if (!impl.normalPriorityQueue.empty())
            {
                std::shared_ptr<JobState> state = std::move(impl.normalPriorityQueue.front());
                impl.normalPriorityQueue.pop_front();
                return state;
            }

            return {};
        }

        void MarkJobComplete(JobSystemImpl& impl, const std::shared_ptr<JobState>& state)
        {
            {
                std::lock_guard<std::mutex> completionLock(state->completionMutex);
                state->complete = true;
            }
            state->completionCondition.notify_all();

            bool allJobsComplete = false;

            {
                std::lock_guard<std::mutex> schedulerLock(impl.schedulerMutex);

                state->schedulerComplete = true;
                for (const std::shared_ptr<JobState>& continuation : state->continuations)
                {
                    VE_ASSERT_MESSAGE(continuation->pendingDependencies > 0,
                                      "Job continuation dependency count underflowed.");

                    --continuation->pendingDependencies;
                    if (continuation->pendingDependencies == 0)
                    {
                        EnqueueReadyJob(impl, continuation);
                    }
                }
                state->continuations.clear();

                VE_ASSERT_MESSAGE(impl.incompleteJobCount > 0, "JobSystem incomplete job count underflowed.");
                --impl.incompleteJobCount;
                allJobsComplete = impl.incompleteJobCount == 0;
            }

            impl.schedulerCondition.notify_all();
            if (allJobsComplete)
            {
                impl.allJobsCompleteCondition.notify_all();
            }
        }

        void ExecuteJob(JobSystemImpl& impl, const std::shared_ptr<JobState>& state)
        {
            try
            {
                state->function();
            }
            catch (...)
            {
                VE_ASSERT_ALWAYS_MESSAGE(false, "Unhandled exception escaped a JobSystem job.");
            }

            MarkJobComplete(impl, state);
        }

        void WorkerLoop(JobSystemImpl& impl)
        {
            for (;;)
            {
                std::shared_ptr<JobState> state;

                {
                    std::unique_lock<std::mutex> lock(impl.schedulerMutex);
                    impl.schedulerCondition.wait(lock, [&]() { return impl.stopping || HasRunnableJob(impl); });

                    if (!HasRunnableJob(impl))
                    {
                        if (impl.stopping)
                        {
                            return;
                        }

                        continue;
                    }

                    state = PopReadyJob(impl);
                }

                if (state)
                {
                    ExecuteJob(impl, state);
                }
            }
        }

        void StopAndJoinWorkers(JobSystemImpl& impl) noexcept
        {
            {
                std::lock_guard<std::mutex> lock(impl.schedulerMutex);
                impl.acceptingJobs = false;
                impl.stopping = true;
            }
            impl.schedulerCondition.notify_all();

            for (std::unique_ptr<Thread>& worker : impl.workers)
            {
                if (worker != nullptr && worker->IsJoinable())
                {
                    static_cast<void>(worker->Join());
                }
            }

            impl.workers.clear();
            impl.highPriorityQueue.clear();
            impl.normalPriorityQueue.clear();
        }
    } // namespace

    JobHandle::JobHandle(std::shared_ptr<JobState> state)
        : state_(std::move(state))
    {
    }

    bool JobHandle::IsValid() const noexcept
    {
        return state_ != nullptr;
    }

    void JobHandle::Reset() noexcept
    {
        state_.reset();
    }

    JobSystem::JobSystem()
        : impl_(std::make_unique<JobSystemImpl>())
    {
    }

    JobSystem::~JobSystem()
    {
        Shutdown();
    }

    ErrorCode JobSystem::Initialize(const JobSystemInitParam& desc)
    {
        const SizeT workerThreadCount = ResolveWorkerThreadCount(desc.workerThreadCount);

        {
            std::lock_guard<std::mutex> lock(impl_->schedulerMutex);
            if (impl_->initialized)
            {
                return ErrorCode::InvalidState;
            }

            impl_->initialized = true;
            impl_->acceptingJobs = true;
            impl_->stopping = false;
            impl_->incompleteJobCount = 0;
            impl_->workerThreadCount = workerThreadCount;
        }

        try
        {
            impl_->workers.reserve(workerThreadCount);
        }
        catch (const std::bad_alloc&)
        {
            Shutdown();
            return ErrorCode::OutOfMemory;
        }

        try
        {
            for (SizeT workerIndex = 0; workerIndex < workerThreadCount; ++workerIndex)
            {
                auto worker = std::make_unique<Thread>();
                ThreadDesc threadDesc;
                threadDesc.name = desc.workerThreadNamePrefix + std::to_string(workerIndex);

                ErrorCode startResult = worker->Start(threadDesc, [impl = impl_.get()]() { WorkerLoop(*impl); });

                if (startResult != ErrorCode::None)
                {
                    Shutdown();
                    return startResult;
                }

                impl_->workers.push_back(std::move(worker));
            }
        }
        catch (const std::bad_alloc&)
        {
            Shutdown();
            return ErrorCode::OutOfMemory;
        }

        return ErrorCode::None;
    }

    void JobSystem::Shutdown() noexcept
    {
        {
            std::unique_lock<std::mutex> lock(impl_->schedulerMutex);
            if (!impl_->initialized)
            {
                return;
            }

            impl_->acceptingJobs = false;
            impl_->allJobsCompleteCondition.wait(lock, [&]() { return impl_->incompleteJobCount == 0; });
        }

        StopAndJoinWorkers(*impl_);

        {
            std::lock_guard<std::mutex> lock(impl_->schedulerMutex);
            impl_->initialized = false;
            impl_->stopping = false;
        }
    }

    bool JobSystem::IsInitialized() const noexcept
    {
        std::lock_guard<std::mutex> lock(impl_->schedulerMutex);
        return impl_->initialized;
    }

    SizeT JobSystem::GetWorkerThreadCount() const noexcept
    {
        std::lock_guard<std::mutex> lock(impl_->schedulerMutex);
        return impl_->workerThreadCount;
    }

    Result<JobSystem::JobFunction> JobSystem::MakeJobFunctionResult(JobFunction function)
    {
        if (!function)
        {
            return Result<JobFunction>::Failure(Error(ErrorCode::InvalidArgument, "Job callable is empty."));
        }

        return Result<JobFunction>::Success(std::move(function));
    }

    Result<JobHandle> JobSystem::ScheduleFunction(const JobDesc& desc,
                                                  Result<JobFunction> functionResult,
                                                  std::span<const JobHandle> dependencies)
    {
        if (!functionResult)
        {
            return Result<JobHandle>::Failure(functionResult.GetError());
        }

        std::shared_ptr<JobState> state;
        try
        {
            state = std::make_shared<JobState>(desc, functionResult.MoveValue());
        }
        catch (const std::bad_alloc&)
        {
            return Result<JobHandle>::Failure(Error(ErrorCode::OutOfMemory, "Job state allocation failed."));
        }

        {
            std::lock_guard<std::mutex> lock(impl_->schedulerMutex);
            if (!impl_->initialized || !impl_->acceptingJobs)
            {
                return Result<JobHandle>::Failure(Error(ErrorCode::InvalidState, "JobSystem is not accepting jobs."));
            }

            std::vector<std::shared_ptr<JobState>> registeredDependencies;
            try
            {
                registeredDependencies.reserve(dependencies.size());

                for (const JobHandle& dependency : dependencies)
                {
                    if (!dependency.IsValid() || dependency.state_->schedulerComplete)
                    {
                        continue;
                    }

                    registeredDependencies.push_back(dependency.state_);
                    dependency.state_->continuations.push_back(state);
                    ++state->pendingDependencies;
                }

                if (state->pendingDependencies == 0)
                {
                    EnqueueReadyJob(*impl_, state);
                }
            }
            catch (const std::bad_alloc&)
            {
                for (const std::shared_ptr<JobState>& dependency : registeredDependencies)
                {
                    RemoveContinuation(*dependency, state);
                }
                return Result<JobHandle>::Failure(Error(ErrorCode::OutOfMemory, "Job dependency registration failed."));
            }

            ++impl_->incompleteJobCount;
        }

        impl_->schedulerCondition.notify_one();
        return Result<JobHandle>::Success(JobHandle(std::move(state)));
    }

    Result<JobHandle>
    JobSystem::ParallelForFunction(const JobDesc& desc, SizeT itemCount, SizeT batchSize, IndexedJobFunction function)
    {
        if (!function)
        {
            return Result<JobHandle>::Failure(Error(ErrorCode::InvalidArgument, "ParallelFor callable is empty."));
        }

        if (itemCount == 0)
        {
            return Schedule(desc, []() {});
        }

        const SizeT batchCount = (itemCount + batchSize - 1) / batchSize;
        std::vector<JobHandle> batchHandles;

        try
        {
            batchHandles.reserve(batchCount);
        }
        catch (const std::bad_alloc&)
        {
            return Result<JobHandle>::Failure(Error(ErrorCode::OutOfMemory, "ParallelFor dependency storage failed."));
        }

        std::shared_ptr<IndexedJobFunction> sharedFunction;
        try
        {
            sharedFunction = std::make_shared<IndexedJobFunction>(std::move(function));
        }
        catch (const std::bad_alloc&)
        {
            return Result<JobHandle>::Failure(Error(ErrorCode::OutOfMemory, "ParallelFor callable storage failed."));
        }

        for (SizeT batchIndex = 0; batchIndex < batchCount; ++batchIndex)
        {
            const SizeT begin = batchIndex * batchSize;
            const SizeT end = std::min(itemCount, begin + batchSize);

            JobDesc batchDesc = desc;
            Result<JobHandle> batchResult = Schedule(batchDesc,
                                                     [sharedFunction, begin, end]()
                                                     {
                                                         for (SizeT index = begin; index < end; ++index)
                                                         {
                                                             (*sharedFunction)(index);
                                                         }
                                                     });

            if (!batchResult)
            {
                return batchResult;
            }

            batchHandles.push_back(batchResult.MoveValue());
        }

        JobDesc finalDesc = desc;
        return ScheduleAfter(finalDesc, std::span<const JobHandle>(batchHandles.data(), batchHandles.size()), []() {});
    }

    void JobSystem::Wait(const JobHandle& handle) const
    {
        if (!handle.IsValid())
        {
            return;
        }

        std::unique_lock<std::mutex> lock(handle.state_->completionMutex);
        handle.state_->completionCondition.wait(lock, [&]() { return handle.state_->complete; });
    }

    void JobSystem::WaitAll(std::span<const JobHandle> handles) const
    {
        for (const JobHandle& handle : handles)
        {
            Wait(handle);
        }
    }

    bool JobSystem::IsComplete(const JobHandle& handle) const noexcept
    {
        if (!handle.IsValid())
        {
            return true;
        }

        std::lock_guard<std::mutex> lock(handle.state_->completionMutex);
        return handle.state_->complete;
    }
} // namespace ve
