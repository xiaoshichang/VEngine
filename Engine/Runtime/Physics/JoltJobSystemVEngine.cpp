#include "Engine/Runtime/Physics/JoltJobSystemVEngine.h"

#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Logging/Log.h"

#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>

namespace ve
{
    namespace
    {
        class ScopedJoltWorkerExecution final
        {
        public:
            explicit ScopedJoltWorkerExecution(bool& flag) noexcept
                : flag_(flag)
                , previousValue_(flag)
            {
                flag_ = true;
            }

            ~ScopedJoltWorkerExecution()
            {
                flag_ = previousValue_;
            }

        private:
            bool& flag_;
            bool previousValue_ = false;
        };

        [[nodiscard]] int ToJoltConcurrency(SizeT workerThreadCount) noexcept
        {
            constexpr SizeT maxInt = static_cast<SizeT>(std::numeric_limits<int>::max());
            return static_cast<int>(std::min(workerThreadCount + 1, maxInt));
        }

        [[noreturn]] void ThrowScheduleFailure(const Error& error)
        {
            std::string message = "Failed to schedule Jolt physics job on VEngine JobSystem: ";
            message += ToString(error.GetCode());
            if (!error.GetMessage().empty())
            {
                message += " - ";
                message += error.GetMessage();
            }

            throw std::runtime_error(message);
        }
    } // namespace

    thread_local bool JoltJobSystemVEngine::executingJoltJobOnWorker_ = false;

    JoltJobSystemVEngine::JoltJobSystemVEngine(ve::JobSystem& jobSystem, JPH::uint maxBarriers)
        : JPH::JobSystemWithBarrier(maxBarriers)
        , jobSystem_(jobSystem)
    {
    }

    JoltJobSystemVEngine::~JoltJobSystemVEngine()
    {
        DrainQueuedJobs();
    }

    int JoltJobSystemVEngine::GetMaxConcurrency() const
    {
        return ToJoltConcurrency(jobSystem_.GetWorkerThreadCount());
    }

    JPH::JobHandle
    JoltJobSystemVEngine::CreateJob(const char* name, JPH::ColorArg color, const JPH::JobSystem::JobFunction& jobFunction, JPH::uint32 dependencyCount)
    {
        Job* job = nullptr;
        try
        {
            job = new Job(name, color, this, jobFunction, dependencyCount);
        }
        catch (const std::bad_alloc&)
        {
            throw;
        }

        JPH::JobHandle handle(job);
        if (dependencyCount == 0)
        {
            QueueJob(job);
        }

        return handle;
    }

    void JoltJobSystemVEngine::QueueJob(Job* job)
    {
        QueueJobInternal(job);
    }

    void JoltJobSystemVEngine::WaitForJobs(Barrier* barrier)
    {
        JPH::JobSystemWithBarrier::WaitForJobs(barrier);
        DrainQueuedJobs();
    }

    void JoltJobSystemVEngine::QueueJobs(Job** jobs, JPH::uint jobCount)
    {
        for (JPH::uint jobIndex = 0; jobIndex < jobCount; ++jobIndex)
        {
            QueueJobInternal(jobs[jobIndex]);
        }
    }

    void JoltJobSystemVEngine::FreeJob(Job* job)
    {
        delete job;
    }

    void JoltJobSystemVEngine::QueueJobInternal(Job* job)
    {
        job->AddRef();

        Result<ve::JobHandle> handle = jobSystem_.Schedule(JobDesc{"JoltPhysics"},
                                                           [job]()
                                                           {
                                                               ScopedJoltWorkerExecution executionScope(executingJoltJobOnWorker_);
                                                               try
                                                               {
                                                                   job->Execute();
                                                               }
                                                               catch (...)
                                                               {
                                                                   job->Release();
                                                                   throw;
                                                               }
                                                               job->Release();
                                                           });
        if (!handle)
        {
            job->Release();
            ThrowScheduleFailure(handle.GetError());
        }

        ve::JobHandle queuedJob = handle.MoveValue();
        try
        {
            std::lock_guard<std::mutex> lock(queuedJobMutex_);
            queuedJobs_.push_back(queuedJob);
        }
        catch (...)
        {
            jobSystem_.Wait(queuedJob);
            throw;
        }
    }

    void JoltJobSystemVEngine::DrainQueuedJobs()
    {
        if (executingJoltJobOnWorker_)
        {
            return;
        }

        for (;;)
        {
            std::vector<ve::JobHandle> jobsToWait;
            {
                std::lock_guard<std::mutex> lock(queuedJobMutex_);
                if (queuedJobs_.empty())
                {
                    return;
                }

                jobsToWait.swap(queuedJobs_);
            }

            jobSystem_.WaitAll(jobsToWait);
        }
    }
} // namespace ve
