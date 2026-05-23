# VEngine Job System Design

## 1. Purpose

The first VEngine Job System is a small C++20 runtime facility for CPU work that can safely run away from the main
thread. It is intentionally narrower than a full task graph or fiber scheduler. The goal is to provide a dependable
first vertical slice for data-parallel work, future scene update helpers, resource preprocessing, and tooling tasks.

The Job System is not a replacement for the engine's named conceptual threads:

- Main Thread: platform messages, lifecycle, application shell, and editor shell.
- Game Thread: scene update, components, scripts, and input consumption.
- Render Thread: render command consumption, render world update, and RHI submission.
- IO Thread: file reads and asynchronous IO scheduling.
- Worker Threads: Job System work.

The first implementation creates only Worker Threads. Game, Render, and IO threads remain separate future systems that
may submit work to the Job System but are not owned by it.

## 2. External References

### 2.1 Unreal Engine

Unreal exposes a general `ParallelFor` helper over its task graph. Its API documentation describes a task-graph-backed
parallel loop, optional debug name, minimum batch size, and a caution that long-running or blocking tasks should not clog
the task graph.

Reference:

- <https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Core/ParallelFor>

Unreal's TaskGraph API also has the concept of tasks queued to named threads or `AnyThread`, which informs VEngine's
separation between conceptual named threads and worker-thread jobs.

References:

- <https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/Async/FTaskGraphInterface/QueueTask>
- <https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/Async/FGraphEvent/IsComplete>

Borrowed ideas:

- Jobs are small units of work.
- `ParallelFor` is a first-class convenience API because repeated independent work is common in engine code.
- Named thread concepts matter, but the first VEngine implementation only schedules worker-thread jobs.
- Blocking inside worker tasks should be discouraged.

Rejected for VEngine v1:

- Full named-thread task routing.
- Fiber-based continuations.
- Work stealing.
- Deep profiling integration.
- Complex task graph visualization.

### 2.2 Unity

Unity's Job System documentation describes jobs as small units of work that can depend on other jobs. It uses
`JobHandle` for dependencies and completion. Unity also exposes `IJobParallelFor`, where each index is expected to be
independent and work is split into batches. Unity documentation notes that batch size affects overhead and distribution.

References:

- <https://docs.unity.cn/Manual/job-system-jobs.html>
- <https://docs.unity.cn/2020.2/Documentation/Manual/JobSystemJobDependencies.html>
- <https://docs.unity.cn/Manual/JobSystemParallelForJobs.html>
- <https://docs.unity.cn/2023.2/Documentation/ScriptReference/Unity.Jobs.IJobParallelFor.html>

Borrowed ideas:

- A `JobHandle` represents completion and can be passed as a dependency.
- Dependencies express required ordering without forcing the caller to block immediately.
- `ParallelFor` requires independent iterations and an explicit batch size.

Rejected for VEngine v1:

- C# safety system.
- Burst-style compilation.
- ECS-specific ownership checks.
- Main-thread-only scheduling restrictions.
- NativeContainer-style memory ownership transfer.

## 3. First Version Scope

The first version supports:

- `JobSystem` object lifecycle with `Initialize()` and `Shutdown()`.
- Worker thread count selection.
- `JobHandle`.
- Scheduling single jobs.
- Scheduling jobs after one dependency.
- Scheduling jobs after many dependencies.
- `Wait()`, `WaitAll()`, and `IsComplete()`.
- `ParallelFor()` with item count and batch size.
- `High` and `Normal` priority queues.

The first version does not support:

- Job cancellation.
- Return values or futures.
- Exception propagation to callers.
- Fiber waits.
- Work stealing.
- Thread affinity.
- Render-thread or game-thread routing.
- PoolAllocator-backed job nodes.

## 4. Thread Planning

### 4.1 Worker Count

`JobSystemDesc::workerThreadCount` controls the number of worker threads. A value of zero means automatic selection:

```text
workerThreadCount = max(1, hardware_concurrency - 1)
```

The default reserves one core for the main/game thread side of the engine. This is a conservative first-stage policy and
can be tuned later for editor builds, dedicated tools, or platform-specific CPU topology.

### 4.2 Thread Names

Worker threads are named with `JobSystemDesc::workerThreadNamePrefix` plus a numeric index:

```text
VEngineJobWorker0
VEngineJobWorker1
...
```

Names are diagnostic only. Correctness must not depend on platform debugger naming support.

### 4.3 Runtime Boundaries

Jobs may:

- Process POD/value data.
- Process copies, snapshots, or immutable views of scene data.
- Build intermediate CPU buffers.
- Import or transform resource data that is already in memory.
- Use `ParallelFor` for independent item ranges.

Jobs must not:

- Call RHI directly.
- Access live `GameObject` state directly.
- Mutate live component containers without an owning system-level synchronization plan.
- Block on long IO.
- Wait on other jobs from inside worker threads in normal use.
- Capture stack references that can outlive their owner.

## 5. Dependencies

### 5.1 Model

VEngine v1 uses lightweight dependency counting rather than a full graph scheduler. A job has a count of incomplete
dependencies. When the count reaches zero, the job becomes runnable and is pushed to the priority queue.

The model is:

```text
Schedule A
Schedule B
Schedule C after A and B

A complete -> C dependency count decrements
B complete -> C dependency count decrements to zero -> C enters runnable queue
```

This gives the important behavior of a task graph without requiring a global graph object, named thread routing, or
graph traversal. It also keeps the public API simple: callers pass one `JobHandle` or a span of handles.

### 5.2 Why Not A Full Task Graph Yet

A full graph would need cycle detection, graph-level lifetime rules, named thread targets, cancellation behavior,
profiling, and more complex debugging. The engine does not need that complexity for the first worker-thread slice.

The first version assumes dependencies are created from already scheduled handles. Since a job cannot depend on a future
handle that has not been returned yet, ordinary API use cannot create cycles.

### 5.3 Invalid Or Completed Handles

An invalid handle is treated like an already complete dependency. This makes it easy to write optional dependency code:

```cpp
JobHandle dependency;
if (hasPreviousWork)
{
    dependency = previousWork;
}

auto result = jobSystem.ScheduleAfter(JobDesc{"Continue"}, dependency, []()
{
    // Runs immediately when dependency is invalid, or after previousWork when valid.
});
```

## 6. Priority

### 6.1 Priority Levels

The first version supports two priorities:

```cpp
enum class JobPriority
{
    Normal,
    High,
};
```

Workers always pop from the high-priority queue before the normal-priority queue. Priority is therefore an ordering hint,
not a hard real-time guarantee.

### 6.2 Why Two Queues

Two queues are enough for the first stage:

- `High`: short jobs needed to unblock the current frame or an interactive tool action.
- `Normal`: ordinary background CPU work.

This avoids premature scheduling policy work. More levels would invite tuning before the engine has real workloads.

### 6.3 Limitations

High priority does not preempt a job that is already running. Long-running jobs can still delay high-priority jobs if all
workers are occupied. The answer is to split long work into smaller jobs or use `ParallelFor` batches.

Repeatedly submitting high-priority work can starve normal work. The first version accepts this tradeoff because high
priority should be reserved for frame-critical work.

## 7. ParallelFor

### 7.1 Purpose

`ParallelFor` exists because many engine operations apply the same independent function to many items:

- Updating transform matrices from component data.
- Building bounds.
- Preparing render proxies from snapshots.
- Running CPU-side mesh or vertex processing.
- Processing imported asset arrays.

### 7.2 API Shape

```cpp
auto result = jobSystem.ParallelFor(
    JobDesc{"BuildBounds"},
    itemCount,
    batchSize,
    [&](SizeT index)
    {
        bounds[index] = BuildBounds(meshes[index]);
    });

jobSystem.Wait(result.GetValue());
```

Each index must be independent. The function may write to `output[index]` and read immutable shared inputs. It must not
write arbitrary shared state unless the caller provides synchronization.

### 7.3 Batch Size

`batchSize` controls how many item indices are grouped into one scheduled job.

Small batches:

- Improve load distribution.
- Help expensive or uneven work finish sooner.
- Increase scheduling overhead.

Large batches:

- Reduce scheduling overhead.
- Improve simple linear data throughput.
- Can leave workers idle when one batch is slower than the others.

First-stage guidance:

```text
Cheap math over arrays: 32-128 items per batch.
Heavy per-item work: 1-16 items per batch.
Unknown workload: start at 32 and measure.
```

### 7.4 First-Version Implementation

VEngine v1 schedules one child job per batch, then schedules one final no-op job that depends on all child jobs. The
returned handle belongs to that final job, so waiting on it waits for the whole `ParallelFor`.

This is simple and correct. It does not yet do dynamic work stealing. Work stealing can be added later when workload
measurements justify it.

## 8. Memory Policy

The first version does not use `PoolAllocator` for job nodes.

Reasons:

- The current `PoolAllocator` is not thread-safe.
- Job scheduling is a multi-threaded path.
- Adding locks around a pool before real profiling may add complexity without a measured benefit.
- Job handles need shared lifetime across callers, dependency lists, and worker queues.

The first version uses standard containers and shared state objects internally. Future options:

- Thread-safe pool wrapper for job nodes.
- Per-worker local pools.
- Generational handle table.
- Small-function storage to reduce callable allocation.

## 9. Public Usage

### 9.1 Initialize And Shutdown

```cpp
JobSystem jobs;
VE_VERIFY(jobs.Initialize(JobSystemDesc{}));

jobs.Shutdown();
```

`Shutdown()` stops accepting new jobs, waits for submitted jobs to complete, wakes workers, and joins them. The first
version does not cancel jobs because cancellation requires explicit user-visible policy and cleanup rules.

### 9.2 Schedule A Job

```cpp
auto result = jobs.Schedule(JobDesc{"BakeChunk"}, []()
{
    BakeChunk();
});

if (result)
{
    jobs.Wait(result.GetValue());
}
```

### 9.3 Schedule With Dependencies

```cpp
JobHandle build = jobs.Schedule(JobDesc{"Build"}, BuildData).GetValue();
JobHandle upload = jobs.ScheduleAfter(JobDesc{"PrepareUpload"}, build, PrepareUploadData).GetValue();

jobs.Wait(upload);
```

For many dependencies:

```cpp
std::array<JobHandle, 2> dependencies{first, second};
JobHandle finalJob = jobs.ScheduleAfter(JobDesc{"Finalize"}, dependencies, Finalize).GetValue();
```

### 9.4 Priority

```cpp
JobDesc desc;
desc.name = "UnblockFrame";
desc.priority = JobPriority::High;

JobHandle handle = jobs.Schedule(desc, []()
{
    BuildSmallFrameCriticalResult();
}).GetValue();
```

High priority should be short and uncommon.

### 9.5 ParallelFor

```cpp
JobHandle handle = jobs.ParallelFor(
    JobDesc{"IntegrateVelocity"},
    positions.size(),
    64,
    [&](SizeT index)
    {
        positions[index] += velocities[index] * deltaSeconds;
    }).GetValue();

jobs.Wait(handle);
```

## 10. Test Plan

The first implementation should add CTest coverage for:

- Initialize and Shutdown.
- Invalid repeated Initialize.
- Schedule single job.
- Wait and IsComplete.
- Multiple jobs executing.
- Single dependency ordering.
- Multiple dependency ordering.
- WaitAll.
- High priority ordering before normal queued jobs.
- ParallelFor covers every item exactly once.
- Shutdown waits for unfinished jobs.
- Scheduling after Shutdown fails.
