#pragma once

#include "Engine/Runtime/Jobs/JobSystem.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/JobSystemWithBarrier.h>

namespace ve
{
    class JoltJobSystemVEngine final : public JPH::JobSystemWithBarrier
    {
    public:
        explicit JoltJobSystemVEngine(ve::JobSystem& jobSystem, JPH::uint maxBarriers);
        ~JoltJobSystemVEngine() override;

        [[nodiscard]] int GetMaxConcurrency() const override;
        [[nodiscard]] JPH::JobHandle CreateJob(const char* name,
                                               JPH::ColorArg color,
                                               const JPH::JobSystem::JobFunction& jobFunction,
                                               JPH::uint32 dependencyCount = 0) override;
        void WaitForJobs(Barrier* barrier) override;

    protected:
        void QueueJob(Job* job) override;
        void QueueJobs(Job** jobs, JPH::uint jobCount) override;
        void FreeJob(Job* job) override;

    private:
        void QueueJobInternal(Job* job);

        ve::JobSystem& jobSystem_;
    };
} // namespace ve
