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

    JoltJobSystemVEngine::JoltJobSystemVEngine(ve::JobSystem& jobSystem, JPH::uint maxBarriers)
        : JPH::JobSystemWithBarrier(maxBarriers)
        , jobSystem_(jobSystem)
    {
    }

    JoltJobSystemVEngine::~JoltJobSystemVEngine()
    {
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

        Result<ve::JobHandle> scheduleResult = jobSystem_.Schedule(JobDesc{"JoltPhysics"},
                                                                   [job]()
                                                                   {
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
        if (!scheduleResult)
        {
            job->Release();
            ThrowScheduleFailure(scheduleResult.GetError());
        }
    }
} // namespace ve
