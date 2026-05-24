# Runtime Thread Model

## 1. Purpose

This document defines the responsibility boundaries and communication rules for VEngine runtime threads.

Current runtime thread groups:

- Main Thread.
- Game Thread.
- Render Thread.
- Job Worker Threads.
- IO Thread.

The goal is to keep ownership explicit. A thread should mutate only the state it owns, and cross-thread communication
should happen through snapshots, command queues, request queues, completion queues, handles, or explicit synchronization
points.

## 2. Thread Ownership Summary

```text
Application
  owns Main Thread behavior

EngineRuntime
  owns long-lived runtime systems

GameThreadSystem
  owns Game Thread

RenderSystem
  owns Render Thread

JobSystem
  owns Job Worker Threads

IOSystem
  owns IO Thread
```

`EngineRuntime` initializes and shuts down these systems in dependency order. Individual systems own their physical
threads and expose only their public service APIs to other modules.

## 3. Main Thread

Owner:

- `Application`.
- Platform window or platform application layer.

Responsibilities:

- Platform startup and shutdown.
- Logging setup call site.
- Native window or native application lifecycle.
- Win32 message pumping on Windows.
- UIKit application and view/layer lifecycle on iOS when that platform lands.
- Debug console command pumping on Windows.
- Native surface lifetime handoff to `RenderSystem`.
- Input event collection into a future input snapshot or command boundary.
- Editor shell event loop when the Editor UI lands.

Must not:

- Directly mutate live `Scene`, `GameObject`, or `Component` state.
- Call RHI APIs directly.
- Execute long blocking file reads on the frame path when `IOSystem` can own the request.
- Directly mutate render-side resources.

Communicates with:

- `GameThreadSystem` through future input snapshots, lifecycle commands, and Editor/game commands.
- `RenderSystem` through explicit lifecycle APIs such as device and swapchain creation/destruction.
- Platform windows through direct platform-layer APIs owned by Main Thread.
- `IOSystem` and `JobSystem` through service APIs when startup, tools, or tests need to schedule work.

Current first-stage behavior:

- Main Thread runs `Application::Run()`.
- Main Thread pumps platform events.
- Main Thread initializes the RHI device and main swapchain through `RenderSystem` public APIs.
- Main Thread connects `GameThreadSystem` to `RenderSystem` after the main swapchain exists, then disconnects it before
  render shutdown.
- Main Thread does not call `Time::Tick()` or `RenderSystem::RenderFrame()` in its loop.

## 4. Game Thread

Owner:

- `GameThreadSystem`.

Responsibilities:

- Own one dedicated physical Game Thread.
- Own Game Thread identity and access validation.
- Own game frame counter and tick phase state.
- Own `Time::Tick()` at `BeginFrame`.
- Own live scene mutation once `Scene` lands.
- Consume input snapshots or commands from Main Thread at a controlled frame boundary.
- Drain resource or IO completions at a controlled frame boundary when `ResourceManager` lands.
- Dispatch component lifecycle and update phases.
- Update transform hierarchy.
- Extract render-safe scene snapshots for `RenderSystem`.
- Drive the current `RenderSystem::RenderFrame()` smoke path until scene frame submission lands.
- Apply game-to-render backpressure at the render submission boundary.

Current tick phases:

```text
BeginFrame
Lifecycle
Update
LateUpdate
TransformUpdate
RenderExtraction
EndFrame
```

Must not:

- Pump OS messages or own native window/view lifecycle.
- Call RHI APIs directly.
- Directly mutate render-side resources.
- Perform arbitrary blocking file IO on the frame path.
- Let Worker Threads directly mutate GameObject hierarchy.

Communicates with:

- Main Thread through future input/lifecycle/editor command queues or snapshots.
- Render Thread through `RenderSystem` render commands now, and scene render snapshots later.
- IO Thread through `IOSystem` completion polling or future `ResourceManager` completion routing.
- Job Worker Threads through `JobSystem` jobs that produce data, not direct live scene mutation.

Current first-stage behavior:

- `GameThreadSystem` starts a dedicated Game Thread during `EngineRuntime::Initialize()`.
- The Game Thread currently runs a continuous loop and advances phase/counter state.
- A temporary `SleepFor(1ms)` pacing point prevents a busy spin until Time, input, scene, and render backpressure land.
- `CheckGameThreadAccess()` and `ValidateGameThreadAccess()` provide the scene mutation rule enforcement hook.

## 5. Render Thread

Owner:

- `RenderSystem`.

Responsibilities:

- Own one dedicated Render Thread.
- Consume render commands.
- Own render-side resource state.
- Own RHI device and main swapchain lifecycle on the Render Thread.
- Build RHI command lists.
- Submit graphics work to the backend GPU queue.
- Present through the main swapchain.
- Coordinate render-side frame slots and backend completion tracking when frames-in-flight land.

Must not:

- Directly access live `Scene`, `GameObject`, or `Component` state.
- Pump platform messages.
- Own Main Thread native window or UIKit lifecycle.
- Execute gameplay or script update.
- Perform source asset import.

Communicates with:

- Game Thread through render commands and future immutable `SceneRenderSnapshot` frame packets.
- Main Thread through explicit `RenderSystem` lifecycle calls for device/swapchain startup and shutdown.
- Job Worker Threads or Resource systems through render commands for upload or render-resource creation work.
- RHI/GPU through backend-specific command queues, fences, and swapchains.

Current first-stage behavior:

- Game Thread calls `RenderSystem::RenderFrame()` during `RenderExtraction`.
- Game Thread initiated render work may call `RenderSystem::Submit(RenderCommand)`.
- `RenderSystem` records the bound Game Thread id and validates Game Thread-only public entry points.
- `RenderCommandQueue` is a lock-free MPSC queue.
- Render Thread is the single consumer.
- Submission failures are fatal startup/runtime errors rather than recoverable `Submit()` return values.
- `RenderSystem::Flush()` is a CPU command queue fence. It does not mean GPU idle.
- RHI device and swapchain lifecycle APIs schedule synchronous work onto the Render Thread.

## 6. Job Worker Threads

Owner:

- `JobSystem`.

Responsibilities:

- Execute scheduled jobs from `JobSystem` queues.
- Support dependencies and continuations.
- Run CPU work that is safe to perform off the Main/Game/Render/IO threads.
- Prepare data for later adoption by the owning system.
- Support parallel work such as math, decoding, import processing, or future scene-independent computation.

Must not:

- Directly mutate live GameObject hierarchy.
- Call RHI APIs directly.
- Pump platform messages.
- Block indefinitely waiting for Main Thread, Game Thread, Render Thread, or IO Thread in ways that can create cycles.
- Own long-lived engine service lifetime.

Communicates with:

- Calling systems through `JobHandle`, dependencies, and `Wait()` / `WaitAll()`.
- Game Thread by producing data that the Game Thread adopts at a frame boundary.
- Resource/import systems by doing CPU-heavy processing requested by those systems.
- Render Thread only through render-safe commands or data prepared for render-side upload.

Current first-stage behavior:

- `JobSystem` owns a worker pool.
- Jobs are scheduled through `JobSystem::Schedule()` and related APIs.
- Completion is observed through `JobHandle`.
- Jobs run on worker threads only.

## 7. IO Thread

Owner:

- `IOSystem`.

Responsibilities:

- Own one dedicated IO Thread.
- Accept asynchronous binary file read requests.
- Execute blocking file reads away from Main Thread and Game Thread.
- Publish completed read results into the `IOSystem` completion queue.
- Preserve file path and file data ownership until the result is consumed.

Must not:

- Parse engine resource formats as a general rule.
- Mutate live scene state.
- Mutate render-side resources.
- Call RHI APIs.
- Dispatch callbacks into arbitrary caller threads.
- Become a general worker pool.

Communicates with:

- Calling systems through `IORequestHandle`.
- Game Thread or future `ResourceManager` through `TryPopCompletedRead()`.
- FileSystem through synchronous file APIs owned by the FileSystem module.

Current first-stage behavior:

- `IOSystem::ReadBinaryFile()` schedules work on the IO Thread.
- `IOSystem::Wait()` and `WaitAll()` wait for request completion.
- `TryPopCompletedRead()` is a global completion polling point for the `IOSystem`.
- The thread that polls a completion owns deciding how data enters live runtime state.

## 8. Communication Matrix

```text
Main -> Game
  Future input snapshots, lifecycle commands, Editor/game commands.
  Current placeholder: GameThreadSystem runs independently; command/input boundary is planned.

Game -> Main
  Future platform requests or Editor notifications through explicit queues.
  No direct platform mutation from Game Thread.

Game -> Render
  Current: GameThreadSystem calls RenderSystem::RenderFrame(); RenderSystem owns RenderFrameContext slots.
  Future: RenderSystem::SubmitFrame(SceneRenderSnapshot).

Render -> Game
  Future frame-consumed notifications, render fences, or diagnostics through explicit state/queues.
  No direct GameObject access from Render Thread.

Main -> Render
  RenderSystem lifecycle APIs: InitializeDevice, CreateMainSwapchain, DestroyMainSwapchain, ShutdownDevice.

Render -> Main
  No direct platform mutation. Surface and window lifecycle remains Main Thread-owned.

Main/Game/Resource -> IO
  IOSystem::ReadBinaryFile().

IO -> Game/Resource
  IOSystem::TryPopCompletedRead() or future ResourceManager-owned completion routing.

Any allowed caller -> Job Workers
  JobSystem::Schedule(), dependencies, ParallelFor.

Job Workers -> owning system
  Job completion handles and prepared data adopted by the owning system at a safe boundary.
```

## 9. Frame Flow

Current first-stage flow:

```text
Main Thread
  Pump platform messages
  Tick application shell
  Own render lifecycle setup/shutdown

Game Thread
  Run continuous frame loop
  BeginFrame: advance Time
  Advance phase and frame counters
  RenderExtraction: call RenderSystem::RenderFrame when connected

Render Thread
  Consume render commands
  Own RHI device/swapchain work
  Resolve RenderFrameToken to RenderFrameContext
  Execute queued RenderFrame smoke commands and release render frame contexts

IO Thread
  Execute queued file reads
  Publish completed read results

Worker Threads
  Execute scheduled jobs
```

Target Milestone 5 scene flow:

```text
Main Thread
  Pump platform messages
  Publish input/lifecycle commands

Game Thread
  Consume input and game commands
  Update Scene
  Extract SceneRenderSnapshot
  Submit frame packet to RenderSystem

Render Thread
  Consume frame packet
  Update render world/resources
  Build RHI command lists
  Submit and present

IO Thread + Worker Threads
  Load and prepare resource data
  Return completion data to Resource/Game ownership boundary
```

## 10. Shutdown Order

`EngineRuntime` should shut down high-level frame producers before lower-level consumers:

```text
GameThreadSystem
RenderSystem
IOSystem
JobSystem
```

Reasoning:

- Game Thread stops producing scene updates and future render snapshots first.
- Render Thread drains or rejects render work and destroys render-side/RHI state next.
- IO Thread stops accepting file requests before lower-level worker infrastructure is torn down.
- Job Worker Threads stop last because higher-level systems may depend on jobs during their own shutdown.

Application-level shutdown should also destroy or disconnect platform surfaces at the right point:

```text
Stop platform/game command submission
Disconnect GameThreadSystem from RenderSystem
Flush or drain RenderSystem render frames in flight
Destroy or flush render work
Destroy swapchain and RHI device through RenderSystem
Stop GameThreadSystem
Destroy native windows/views on Main Thread
Shutdown remaining EngineRuntime services
```

## 11. Ownership Rules

Hard rules:

- Main Thread owns platform lifecycle.
- Game Thread owns live scene mutation.
- Render Thread owns render-side state and RHI submission.
- IO Thread owns blocking file reads scheduled through `IOSystem`.
- Job Worker Threads own only the execution of scheduled job bodies.

Data crossing rules:

- Cross-thread data should be copied, moved, snapshotted, or referenced through stable handles.
- Live `GameObject` and `Component` pointers must not cross to Render Thread.
- RHI objects must not cross to Game Thread as mutable objects.
- File bytes from IO become runtime resource or scene state only when adopted by the owning system.
- Worker jobs may prepare data, but the owning thread decides when prepared data becomes live state.

Synchronization rules:

- Prefer queues, snapshots, handles, and explicit wait/fence APIs over shared mutable state.
- Avoid cyclic waits between Main, Game, Render, IO, and Worker Threads.
- `RenderSystem::Flush()` waits for CPU render commands, not GPU idle.
- Future GPU frame lifetime must be tracked through RHI fences or backend completion objects.
- Future Game-to-Render snapshots should use bounded queues or backpressure so Game Thread cannot run unbounded ahead of
  Render Thread.

## 12. Current Gaps

These boundaries are intentional future work:

- Main-to-Game command queue.
- Input snapshot object and publication rules.
- Game-to-Render `SceneRenderSnapshot` submission API.
- ResourceManager completion routing from IO and jobs back to Game Thread.
- Render-side frames-in-flight implementation.
- Game Thread frame pacing tied to Time, window focus, pause state, and render backpressure.
- Editor read-only scene inspection snapshots or Game Thread query commands.
