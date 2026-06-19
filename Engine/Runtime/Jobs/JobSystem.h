#pragma once

#include "Engine/Runtime/Core/NonCopyable.h"
#include "Engine/Runtime/Core/Result.h"
#include "Engine/Runtime/Core/Types.h"

#include <functional>
#include <memory>
#include <new>
#include <span>
#include <string>
#include <utility>

namespace ve
{
    struct JobState;
    struct JobSystemImpl;

    /// Controls the queue used by a scheduled job.
    ///
    /// Priority is an ordering hint inside the worker-thread queues. A high-priority job is selected before queued
    /// normal jobs, but it does not interrupt work that is already running on a worker.
    enum class JobPriority
    {
        Normal,
        High,
    };

    /// Describes one scheduled job.
    ///
    /// The name is a diagnostic label for debugging and future profiling. Empty names are allowed. The first
    /// implementation stores the name but does not require platform-level task naming support.
    struct JobDesc
    {
        std::string name;
        JobPriority priority = JobPriority::Normal;
    };

    /// Describes the worker pool created by JobSystem::Initialize().
    struct JobSystemInitParam
    {
        /// Number of worker threads to create. A value of zero means max(1, hardware_concurrency - 1).
        SizeT workerThreadCount = 0;

        /// Prefix used for worker thread diagnostic names. The worker index is appended to this prefix.
        std::string workerThreadNamePrefix = "VEngineJobWorker";
    };

    /// Lightweight completion handle returned by JobSystem scheduling APIs.
    ///
    /// A handle may be copied freely and can outlive the JobSystem object that produced it. It does not own a worker
    /// thread and does not expose the callable. Invalid handles are treated as already complete when used as
    /// dependencies.
    class JobHandle
    {
    public:
        JobHandle() = default;

        /// Returns true when this handle refers to a scheduled job state.
        [[nodiscard]] bool IsValid() const noexcept;

        /// Clears this handle. Waiting on a cleared handle is a no-op.
        void Reset() noexcept;

    private:
        friend class JobSystem;

        explicit JobHandle(std::shared_ptr<JobState> state);

        std::shared_ptr<JobState> state_;
    };

    /// Owns a first-stage worker-thread job scheduler.
    ///
    /// JobSystem is intentionally narrower than a full task graph. It supports single jobs, dependency-counted
    /// continuations, waiting, two priority queues, and ParallelFor batching. Jobs run on worker threads only; they
    /// must not call RHI directly or access live GameObject state without a higher-level synchronization plan.
    class JobSystem : public NonMovable
    {
    public:
        JobSystem();
        ~JobSystem();

        /// Creates worker threads and starts accepting jobs.
        ///
        /// Returns InvalidState when called while already initialized. If workerThreadCount is zero, the implementation
        /// creates max(1, hardware_concurrency - 1) workers. Partial initialization failures stop and join workers that
        /// were already created.
        [[nodiscard]] ErrorCode Initialize(const JobSystemInitParam& desc);

        /// Stops accepting jobs, waits for submitted jobs to complete, wakes workers, and joins them.
        ///
        /// Shutdown does not cancel work. It is safe to call on an uninitialized system, where it becomes a no-op.
        void Shutdown() noexcept;

        /// Returns true after Initialize() succeeds and before Shutdown() finishes.
        [[nodiscard]] bool IsInitialized() const noexcept;

        /// Returns the number of worker threads created by the last successful Initialize().
        [[nodiscard]] SizeT GetWorkerThreadCount() const noexcept;

        /// Schedules a job with no dependencies.
        ///
        /// The callable is copied or moved into the job state and invoked once on a worker thread. It must be callable
        /// as `void()`. Captured references must remain valid until the job completes.
        template<typename Callable>
        [[nodiscard]] Result<JobHandle> Schedule(const JobDesc& desc, Callable&& callable)
        {
            return ScheduleFunction(desc, MakeJobFunction(std::forward<Callable>(callable)), {});
        }

        /// Schedules a job that becomes runnable after one dependency completes.
        ///
        /// Invalid or already-complete dependencies do not delay the job. The dependency only controls ordering; it
        /// does not transfer ownership of captured data or make unsafe shared state access safe.
        template<typename Callable>
        [[nodiscard]] Result<JobHandle> ScheduleAfter(const JobDesc& desc, const JobHandle& dependency, Callable&& callable)
        {
            std::span<const JobHandle> dependencies(&dependency, 1);
            return ScheduleFunction(desc, MakeJobFunction(std::forward<Callable>(callable)), dependencies);
        }

        /// Schedules a job that becomes runnable after all supplied dependencies complete.
        ///
        /// The span is consumed during the call and does not need to remain valid afterward. Invalid handles are
        /// ignored.
        template<typename Callable>
        [[nodiscard]] Result<JobHandle> ScheduleAfter(const JobDesc& desc, std::span<const JobHandle> dependencies, Callable&& callable)
        {
            return ScheduleFunction(desc, MakeJobFunction(std::forward<Callable>(callable)), dependencies);
        }

        /// Schedules independent work for the half-open index range [0, itemCount).
        ///
        /// The callable is invoked as `void(SizeT index)`. batchSize controls how many indices are grouped into one
        /// child job. The returned handle completes after every batch job completes. Each index must be independent
        /// unless the caller provides its own synchronization for shared state.
        template<typename Callable>
        [[nodiscard]] Result<JobHandle> ParallelFor(const JobDesc& desc, SizeT itemCount, SizeT batchSize, Callable&& callable)
        {
            if (batchSize == 0)
            {
                return Result<JobHandle>::Failure(Error(ErrorCode::InvalidArgument, "ParallelFor batchSize must be greater than zero."));
            }

            try
            {
                auto sharedCallable = std::make_shared<std::decay_t<Callable>>(std::forward<Callable>(callable));
                return ParallelForFunction(desc, itemCount, batchSize, [sharedCallable = std::move(sharedCallable)](SizeT index) { (*sharedCallable)(index); });
            }
            catch (const std::bad_alloc&)
            {
                return Result<JobHandle>::Failure(Error(ErrorCode::OutOfMemory, "ParallelFor callable allocation failed."));
            }
        }

        /// Blocks until the supplied handle completes.
        ///
        /// Waiting on an invalid handle returns immediately. Waiting from inside worker jobs is discouraged because it
        /// can reduce available parallelism and can deadlock if callers create cyclic waiting outside the dependency
        /// API.
        void Wait(const JobHandle& handle) const;

        /// Blocks until every supplied handle completes.
        ///
        /// Invalid handles are skipped. The span is only used during the call.
        void WaitAll(std::span<const JobHandle> handles) const;

        /// Returns true when the supplied handle is invalid or has completed.
        [[nodiscard]] bool IsComplete(const JobHandle& handle) const noexcept;

    private:
        using JobFunction = std::function<void()>;
        using IndexedJobFunction = std::function<void(SizeT)>;

        [[nodiscard]] static Result<JobFunction> MakeJobFunctionResult(JobFunction function);

        template<typename Callable>
        [[nodiscard]] static Result<JobFunction> MakeJobFunction(Callable&& callable)
        {
            try
            {
                return MakeJobFunctionResult(JobFunction(std::forward<Callable>(callable)));
            }
            catch (const std::bad_alloc&)
            {
                return Result<JobFunction>::Failure(Error(ErrorCode::OutOfMemory, "Job callable allocation failed."));
            }
        }

        [[nodiscard]] Result<JobHandle> ScheduleFunction(const JobDesc& desc, Result<JobFunction> functionResult, std::span<const JobHandle> dependencies);

        [[nodiscard]] Result<JobHandle> ParallelForFunction(const JobDesc& desc, SizeT itemCount, SizeT batchSize, IndexedJobFunction function);

        std::unique_ptr<JobSystemImpl> impl_;
    };
} // namespace ve
