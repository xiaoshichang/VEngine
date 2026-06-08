#include "Engine/Runtime/Core/Error.h"
#include "Engine/Runtime/Jobs/JobSystem.h"
#include "Engine/Runtime/Threading/Atomic.h"
#include "Engine/Runtime/Threading/Synchronization.h"
#include "Engine/Runtime/Threading/Thread.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <mutex>
#include <span>
#include <vector>

namespace
{
    bool Expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "FAILED: " << message << '\n';
        }

        return condition;
    }

    bool ExpectOk(ve::ErrorCode result, const char* message)
    {
        if (result == ve::ErrorCode::None)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result) << '\n';
        return false;
    }

    bool ExpectJobHandleResult(const ve::Result<ve::JobHandle>& result, const char* message)
    {
        if (result)
        {
            return true;
        }

        std::cerr << "FAILED: " << message << ": " << ve::ToString(result.GetError().GetCode());

        if (!result.GetError().GetMessage().empty())
        {
            std::cerr << ": " << result.GetError().GetMessage();
        }

        std::cerr << '\n';
        return false;
    }

    ve::JobSystemInitParam OneWorkerDesc()
    {
        ve::JobSystemInitParam desc;
        desc.workerThreadCount = 1;
        desc.workerThreadNamePrefix = "JobSystemTestWorker";
        return desc;
    }

    bool TestInitializeAndShutdown()
    {
        bool passed = true;

        ve::JobSystem jobs;
        passed &= ExpectOk(jobs.Initialize(OneWorkerDesc()), "JobSystem should initialize");
        passed &= Expect(jobs.IsInitialized(), "Initialized JobSystem should report initialized");
        passed &= Expect(jobs.GetWorkerThreadCount() == 1, "JobSystem should use requested worker count");

        jobs.Shutdown();
        passed &= Expect(!jobs.IsInitialized(), "Shutdown JobSystem should report uninitialized");

        jobs.Shutdown();

        return passed;
    }

    bool TestRepeatedInitializeFails()
    {
        bool passed = true;

        ve::JobSystem jobs;
        passed &= ExpectOk(jobs.Initialize(OneWorkerDesc()), "Initial Initialize should succeed");

        const ve::ErrorCode repeatedInitialize = jobs.Initialize(OneWorkerDesc());
        passed &= Expect(repeatedInitialize != ve::ErrorCode::None, "Repeated Initialize should fail");
        passed &=
            Expect(repeatedInitialize == ve::ErrorCode::InvalidState, "Repeated Initialize should report InvalidState");

        jobs.Shutdown();
        return passed;
    }

    bool TestScheduleSingleJobWaitAndIsComplete()
    {
        bool passed = true;

        ve::JobSystem jobs;
        passed &= ExpectOk(jobs.Initialize(OneWorkerDesc()), "JobSystem should initialize for single job test");

        ve::AtomicInt32 value{0};
        ve::Result<ve::JobHandle> result =
            jobs.Schedule(ve::JobDesc{"SingleJob"}, [&]() { value.store(42, std::memory_order_release); });

        passed &= ExpectJobHandleResult(result, "Schedule should return a job handle");
        if (result)
        {
            ve::JobHandle handle = result.MoveValue();
            passed &= Expect(handle.IsValid(), "Scheduled job handle should be valid");
            jobs.Wait(handle);
            passed &= Expect(jobs.IsComplete(handle), "Waited job should be complete");
            passed &= Expect(value.load(std::memory_order_acquire) == 42, "Scheduled job should run");
        }

        jobs.Shutdown();
        return passed;
    }

    bool TestMultipleJobsExecute()
    {
        bool passed = true;

        ve::JobSystem jobs;
        ve::JobSystemInitParam desc = OneWorkerDesc();
        desc.workerThreadCount = 2;
        passed &= ExpectOk(jobs.Initialize(desc), "JobSystem should initialize for multiple jobs test");

        constexpr int JobCount = 32;
        ve::AtomicInt32 counter{0};
        std::vector<ve::JobHandle> handles;
        handles.reserve(JobCount);

        for (int jobIndex = 0; jobIndex < JobCount; ++jobIndex)
        {
            ve::Result<ve::JobHandle> result =
                jobs.Schedule(ve::JobDesc{"CountJob"}, [&]() { counter.fetch_add(1, std::memory_order_acq_rel); });

            passed &= ExpectJobHandleResult(result, "Schedule should succeed for each count job");
            if (result)
            {
                handles.push_back(result.MoveValue());
            }
        }

        jobs.WaitAll(handles);
        passed &= Expect(counter.load(std::memory_order_acquire) == JobCount, "All scheduled jobs should execute");

        jobs.Shutdown();
        return passed;
    }

    bool TestSingleDependencyOrders()
    {
        bool passed = true;

        ve::JobSystem jobs;
        passed &= ExpectOk(jobs.Initialize(OneWorkerDesc()), "JobSystem should initialize for dependency test");

        ve::ManualResetEvent releaseFirst;
        std::mutex mutex;
        std::vector<int> order;

        ve::Result<ve::JobHandle> first = jobs.Schedule(ve::JobDesc{"First"},
                                                        [&]()
                                                        {
                                                            releaseFirst.Wait();
                                                            std::lock_guard<std::mutex> lock(mutex);
                                                            order.push_back(1);
                                                        });

        passed &= ExpectJobHandleResult(first, "First dependency job should schedule");

        ve::Result<ve::JobHandle> second = first
                                               ? jobs.ScheduleAfter(ve::JobDesc{"Second"},
                                                                    first.GetValue(),
                                                                    [&]()
                                                                    {
                                                                        std::lock_guard<std::mutex> lock(mutex);
                                                                        order.push_back(2);
                                                                    })
                                               : ve::Result<ve::JobHandle>::Failure(ve::Error(ve::ErrorCode::Unknown));

        passed &= ExpectJobHandleResult(second, "Dependent job should schedule");
        releaseFirst.Set();

        if (second)
        {
            jobs.Wait(second.GetValue());
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            passed &= Expect(order == std::vector<int>({1, 2}), "Dependent job should run after dependency");
        }

        jobs.Shutdown();
        return passed;
    }

    bool TestMultipleDependenciesOrder()
    {
        bool passed = true;

        ve::JobSystem jobs;
        ve::JobSystemInitParam desc = OneWorkerDesc();
        desc.workerThreadCount = 2;
        passed &= ExpectOk(jobs.Initialize(desc), "JobSystem should initialize for multi-dependency test");

        ve::ManualResetEvent releaseDependencies;
        ve::AtomicInt32 completedDependencies{0};

        ve::Result<ve::JobHandle> first =
            jobs.Schedule(ve::JobDesc{"FirstDependency"},
                          [&]()
                          {
                              releaseDependencies.Wait();
                              completedDependencies.fetch_add(1, std::memory_order_acq_rel);
                          });

        ve::Result<ve::JobHandle> second =
            jobs.Schedule(ve::JobDesc{"SecondDependency"},
                          [&]()
                          {
                              releaseDependencies.Wait();
                              completedDependencies.fetch_add(1, std::memory_order_acq_rel);
                          });

        passed &= ExpectJobHandleResult(first, "First dependency should schedule");
        passed &= ExpectJobHandleResult(second, "Second dependency should schedule");

        std::array<ve::JobHandle, 2> dependencies{};
        if (first && second)
        {
            dependencies = {first.GetValue(), second.GetValue()};
        }

        ve::AtomicInt32 observedDependencyCount{-1};
        ve::Result<ve::JobHandle> final =
            jobs.ScheduleAfter(ve::JobDesc{"AfterBoth"},
                               dependencies,
                               [&]()
                               {
                                   observedDependencyCount.store(completedDependencies.load(std::memory_order_acquire),
                                                                 std::memory_order_release);
                               });

        passed &= ExpectJobHandleResult(final, "Final multi-dependency job should schedule");
        releaseDependencies.Set();

        if (final)
        {
            jobs.Wait(final.GetValue());
        }

        passed &= Expect(observedDependencyCount.load(std::memory_order_acquire) == 2,
                         "Final job should observe both dependencies complete");

        jobs.Shutdown();
        return passed;
    }

    bool TestWaitAll()
    {
        bool passed = true;

        ve::JobSystem jobs;
        passed &= ExpectOk(jobs.Initialize(OneWorkerDesc()), "JobSystem should initialize for WaitAll test");

        ve::AtomicInt32 counter{0};
        std::array<ve::JobHandle, 3> handles{};

        for (ve::JobHandle& handle : handles)
        {
            ve::Result<ve::JobHandle> result =
                jobs.Schedule(ve::JobDesc{"WaitAllJob"}, [&]() { counter.fetch_add(1, std::memory_order_acq_rel); });

            passed &= ExpectJobHandleResult(result, "WaitAll job should schedule");
            if (result)
            {
                handle = result.MoveValue();
            }
        }

        jobs.WaitAll(handles);
        passed &= Expect(counter.load(std::memory_order_acquire) == 3, "WaitAll should wait for every handle");

        jobs.Shutdown();
        return passed;
    }

    bool TestHighPriorityRunsBeforeQueuedNormalJobs()
    {
        bool passed = true;

        ve::JobSystem jobs;
        passed &= ExpectOk(jobs.Initialize(OneWorkerDesc()), "JobSystem should initialize for priority test");

        ve::ManualResetEvent releaseBlockingJob;
        ve::ManualResetEvent blockingJobStarted;
        std::mutex mutex;
        std::vector<int> order;

        ve::Result<ve::JobHandle> blocker = jobs.Schedule(ve::JobDesc{"BlockingNormal"},
                                                          [&]()
                                                          {
                                                              blockingJobStarted.Set();
                                                              releaseBlockingJob.Wait();
                                                          });

        passed &= ExpectJobHandleResult(blocker, "Blocking job should schedule");
        blockingJobStarted.Wait();

        ve::Result<ve::JobHandle> normal = jobs.Schedule(ve::JobDesc{"QueuedNormal"},
                                                         [&]()
                                                         {
                                                             std::lock_guard<std::mutex> lock(mutex);
                                                             order.push_back(1);
                                                         });

        ve::JobDesc highDesc;
        highDesc.name = "QueuedHigh";
        highDesc.priority = ve::JobPriority::High;
        ve::Result<ve::JobHandle> high = jobs.Schedule(highDesc,
                                                       [&]()
                                                       {
                                                           std::lock_guard<std::mutex> lock(mutex);
                                                           order.push_back(2);
                                                       });

        passed &= ExpectJobHandleResult(normal, "Queued normal job should schedule");
        passed &= ExpectJobHandleResult(high, "Queued high job should schedule");

        releaseBlockingJob.Set();
        if (normal && high)
        {
            std::array<ve::JobHandle, 2> handles{normal.GetValue(), high.GetValue()};
            jobs.WaitAll(handles);
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            passed &=
                Expect(order == std::vector<int>({2, 1}), "High-priority queued job should run before normal job");
        }

        jobs.Shutdown();
        return passed;
    }

    bool TestParallelForCoversEachItemOnce()
    {
        bool passed = true;

        ve::JobSystem jobs;
        ve::JobSystemInitParam desc = OneWorkerDesc();
        desc.workerThreadCount = 3;
        passed &= ExpectOk(jobs.Initialize(desc), "JobSystem should initialize for ParallelFor test");

        constexpr ve::SizeT ItemCount = 257;
        std::vector<int> visits(ItemCount, 0);
        std::mutex visitsMutex;

        ve::Result<ve::JobHandle> result = jobs.ParallelFor(ve::JobDesc{"VisitItems"},
                                                            ItemCount,
                                                            17,
                                                            [&](ve::SizeT index)
                                                            {
                                                                std::lock_guard<std::mutex> lock(visitsMutex);
                                                                ++visits[index];
                                                            });

        passed &= ExpectJobHandleResult(result, "ParallelFor should schedule");
        if (result)
        {
            jobs.Wait(result.GetValue());
        }

        {
            std::lock_guard<std::mutex> lock(visitsMutex);
            passed &= Expect(std::all_of(visits.begin(), visits.end(), [](int count) { return count == 1; }),
                             "ParallelFor should visit every index exactly once");
        }

        ve::Result<ve::JobHandle> invalidBatch =
            jobs.ParallelFor(ve::JobDesc{"InvalidBatch"}, ItemCount, 0, [](ve::SizeT) {});
        passed &= Expect(!invalidBatch, "ParallelFor should reject zero batch size");
        if (!invalidBatch)
        {
            passed &= Expect(invalidBatch.GetError().GetCode() == ve::ErrorCode::InvalidArgument,
                             "ParallelFor zero batch should report InvalidArgument");
        }

        jobs.Shutdown();
        return passed;
    }

    bool TestShutdownWaitsForUnfinishedJob()
    {
        bool passed = true;

        ve::JobSystem jobs;
        passed &= ExpectOk(jobs.Initialize(OneWorkerDesc()), "JobSystem should initialize for shutdown wait test");

        ve::ManualResetEvent jobStarted;
        ve::ManualResetEvent releaseJob;
        ve::AtomicBool jobCompleted{false};

        ve::Result<ve::JobHandle> result = jobs.Schedule(ve::JobDesc{"ShutdownWait"},
                                                         [&]()
                                                         {
                                                             jobStarted.Set();
                                                             releaseJob.Wait();
                                                             jobCompleted.store(true, std::memory_order_release);
                                                         });

        passed &= ExpectJobHandleResult(result, "Shutdown wait job should schedule");
        jobStarted.Wait();

        ve::Thread releaser;
        passed &= ExpectOk(releaser.Start(ve::ThreadDesc{"ShutdownWaitReleaser"},
                                          [&]()
                                          {
                                              ve::SleepFor(std::chrono::milliseconds(20));
                                              releaseJob.Set();
                                          }),
                           "Shutdown wait releaser should start");

        jobs.Shutdown();
        passed &=
            Expect(jobCompleted.load(std::memory_order_acquire), "Shutdown should wait until unfinished job completes");
        passed &= Expect(releaser.Join(), "Shutdown wait releaser should join");

        return passed;
    }

    bool TestScheduleAfterShutdownFails()
    {
        bool passed = true;

        ve::JobSystem jobs;
        passed &=
            ExpectOk(jobs.Initialize(OneWorkerDesc()), "JobSystem should initialize before shutdown failure test");
        jobs.Shutdown();

        ve::Result<ve::JobHandle> result = jobs.Schedule(ve::JobDesc{"ShouldFail"}, []() {});

        passed &= Expect(!result, "Schedule after Shutdown should fail");
        if (!result)
        {
            passed &= Expect(result.GetError().GetCode() == ve::ErrorCode::InvalidState,
                             "Schedule after Shutdown should report InvalidState");
        }

        return passed;
    }
} // namespace

int main()
{
    bool passed = true;

    passed &= TestInitializeAndShutdown();
    passed &= TestRepeatedInitializeFails();
    passed &= TestScheduleSingleJobWaitAndIsComplete();
    passed &= TestMultipleJobsExecute();
    passed &= TestSingleDependencyOrders();
    passed &= TestMultipleDependenciesOrder();
    passed &= TestWaitAll();
    passed &= TestHighPriorityRunsBeforeQueuedNormalJobs();
    passed &= TestParallelForCoversEachItemOnce();
    passed &= TestShutdownWaitsForUnfinishedJob();
    passed &= TestScheduleAfterShutdownFails();

    if (passed)
    {
        std::cout << "VEngineJobSystemTests passed" << '\n';
        return 0;
    }

    return 1;
}
