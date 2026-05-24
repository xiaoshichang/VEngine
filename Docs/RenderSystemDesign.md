# RenderSystem Design

## 1. Purpose

`RenderSystem` is the runtime service that owns render-thread lifecycle, render command execution, and first-stage RHI
device/swapchain lifecycle. It is managed by `EngineRuntime` alongside `JobSystem` and `IOSystem`.

The first version intentionally keeps the renderer narrow while making the runtime boundary real:

- Start and stop the Render Thread.
- Accept commands from Game Thread, ResourceSystem, jobs, and tests.
- Execute commands on the Render Thread.
- Provide CPU-side flushing for submitted render commands.
- Initialize and shut down the selected RHI device on the Render Thread.
- Create and destroy the main swapchain from the platform window surface.
- Render a minimal triangle frame as the first Player/Editor rendering smoke path.
- Leave RenderWorld, render-resource registries, upload scheduling, frame graph/pass orchestration, and viewport
  registration for later renderer work.

## 2. Relationship To RHI

`RenderSystem` and RHI documents have different responsibilities.

`RenderSystem` defines:

- Runtime service lifecycle.
- Render Thread ownership.
- Render command queue semantics.
- CPU-side command flushing and shutdown draining.
- How higher-level modules submit work to the Render Thread.
- How Application connects a platform surface to the main RHI swapchain.

RHI documents define:

- Common graphics backend object model.
- Device, queue, command list, resource, shader, pipeline, and binding concepts.
- D3D11, D3D12, Metal, and future backend mapping.
- Command recording, presentation, and resource-state behavior.

This separation keeps `RenderSystem` focused on service, thread, and lifecycle orchestration. The RHI remains focused on
backend object behavior; `RenderSystem` decides when those objects are created, used for the main frame smoke path, and
destroyed.

## 3. Naming And Ownership

Long-lived runtime services owned by `EngineRuntime` use `System` in their type names:

```text
JobSystem
IOSystem
RenderSystem
```

Conceptual or physical threads keep thread names such as Game Thread, Render Thread, IO Thread, and Worker Thread.

`EngineRuntime` initializes services in dependency order:

```text
JobSystem
IOSystem
RenderSystem
```

Shutdown runs in reverse order:

```text
RenderSystem
IOSystem
JobSystem
```

This order prevents Render Thread commands from running while lower-level runtime services are already being torn down.

## 4. First Version Scope

The first version includes:

- `RenderSystemDesc`.
- `RenderSystem`.
- `RenderThreadContext`.
- `RenderCommand`.
- One Render Thread owned by `RenderSystem`.
- A lock-free multi-producer, single-consumer render command queue.
- `Submit()` for render commands.
- `Flush()` for CPU-side command completion.
- Integration into `EngineRuntime`.
- RHI device lifecycle APIs.
- Main swapchain lifecycle APIs.
- A minimal `RenderFrame()` path that clears, draws one triangle, submits, and presents.
- Unit tests for command execution, flushing, shutdown draining, and runtime access.

The first version does not create or own:

- RHI device.
- Swapchain.
- Platform window or Metal layer.
- Render resources.
- RenderWorld or RenderScene.
- Frame graph.
- Mesh, material, texture, or UI rendering.

Milestone 5 expands this scope with scene frame submission, render-safe snapshots, and render-side frame contexts. That
vertical slice is described in `Docs/SceneRenderingVerticalSlice.md`.

## 5. Command Queue Model

The command queue is a lock-free MPSC queue:

```text
Many producers:
  Game Thread
  ResourceSystem
  JobSystem worker jobs
  tests

Single consumer:
  Render Thread
```

`LockFreeMpscQueue<T>` owns the reusable MPSC publication algorithm. `RenderCommandQueue` is the render-layer wrapper
that stores `RenderCommand` payloads and exposes Render Thread consumer operations. `Submit()` allocates one command
node and appends it atomically. The Render Thread owns command execution and node deletion. The queue may use a
mutex-free wake mechanism such as `Semaphore` or an event, but command publication itself must not require a queue
mutex.

Commands are represented as:

```cpp
struct RenderCommand
{
    std::string debugName;
    RenderCommandFunction function;
};
```

`debugName` is diagnostic and may be empty. `function` must be callable as `void(RenderThreadContext&)`.

Captured data must remain valid until the command runs. Future render-resource handles should replace raw captured
engine pointers where possible.

## 6. Render Thread Context

`RenderThreadContext` is passed to every command. The first version exposes only render-thread identity:

```cpp
ThreadId GetRenderThreadId() const noexcept;
```

Future versions may expose RHI device access, frame index, upload helpers, or render-resource registries through this
context. Game Thread systems should not infer RHI access before those APIs exist.

## 7. Flush And Shutdown Semantics

`Flush()` waits until all commands submitted before the flush call have completed on the Render Thread.

`RenderCommandFence` is the lighter CPU command-stream fence used by Game Thread frame pacing. It inserts a private
signal command into the Render Thread queue and can be waited by the Game Thread without flushing unrelated future
commands. `GameThreadSystem` uses two of these fences as a first-stage `FFrameEndSync`-style frame-end synchronizer:
the current frame inserts one fence, then the Game Thread waits on the previous fence when one-frame thread lag is
allowed.

`Flush()` is a CPU render command queue fence. It does not mean:

- swapchain presented.
- resource uploads are visible to shaders.

Shutdown behavior:

1. Stop accepting new commands.
2. Wake the Render Thread.
3. Drain commands that were accepted before shutdown.
4. Join the Render Thread.
5. Clear runtime state.

Commands submitted after shutdown starts fail with `ErrorCode::InvalidState`.

## 8. Lifecycle Rules

A standalone `RenderSystem` may be initialized again after `Shutdown()`. This matches `JobSystem` and `IOSystem` and
keeps unit tests straightforward.

`EngineRuntime` remains one-shot. A single `EngineRuntime` object cannot be initialized again after a completed
lifecycle.

Repeated `RenderSystem::Initialize()` while running is a fatal startup error. `RenderSystem::Initialize()` logs the
failure and terminates instead of returning a recoverable error code.

`Submit()` and `SubmitFunction()` submission errors are fatal. Invalid state, empty command functions, or queue push
failures are logged as fatal errors and terminate the process.

`RenderFrame()` also treats frame-slot acquire, command submission, and Render Thread frame execution failures as fatal
errors.

## 9. RHI Device And Swapchain Lifecycle

### 9.1 Reference Direction

Unreal and Unity both separate long-lived graphics lifecycle ownership from ordinary per-frame rendering commands.

Unreal exposes render-thread enqueue APIs and has a deeper Render Thread / RHI Thread / RHI command-list stack. The
important pattern for VEngine is not the exact internal queue implementation, but the ownership split: high-level engine
code does not directly mutate live render/RHI state; rendering work is handed to the render-side owner.

Unity exposes graphics-device lifecycle callbacks to native rendering plugins and requires render-thread events for
render-thread work. The useful pattern is similar: device lifecycle is a graphics-system concern, while actual
render-thread execution is scheduled into the render side rather than performed directly from arbitrary caller threads.

VEngine should follow this split:

- `RenderSystem` owns the lifecycle API and public state transitions.
- RHI objects are created, used, and destroyed on the Render Thread.
- Ordinary external callers should not create or destroy RHI devices by submitting ad hoc `RenderCommand` lambdas.
- `RenderSystem` may implement its own lifecycle APIs internally by enqueueing synchronous render-thread operations.

### 9.2 Public Lifecycle API

RHI device and swapchain lifecycle should be explicit `RenderSystem` API, not an unstructured render command submitted
by arbitrary callers.

First RHI-facing API shape:

```cpp
enum class RenderBackend
{
    D3D11,
    D3D12,
    Metal,
};

struct RenderDeviceDesc
{
    RenderBackend backend = RenderBackend::D3D12;
    bool enableDebugDevice = false;
};

struct RenderSystemDesc
{
    std::string threadName = "VEngineRenderThread";
    RenderDeviceDesc device;
};

struct RenderSurfaceDesc
{
    void* nativeWindow = nullptr;
    void* nativeLayer = nullptr;
    UInt32 width = 0;
    UInt32 height = 0;
    rhi::RhiFormat colorFormat = rhi::RhiFormat::Bgra8Unorm;
    UInt32 bufferCount = 2;
};

ErrorCode InitializeDevice(const RenderDeviceDesc& desc);
ErrorCode CreateMainSwapchain(const RenderSurfaceDesc& desc);
void DestroyMainSwapchain() noexcept;
void ShutdownDevice() noexcept;
bool HasDevice() const noexcept;
RenderBackend GetDeviceBackend() const noexcept;
void RenderFrame();
```

`Application` obtains native surface information through the `Window` base interface instead of branching on a platform
macro itself. Platform-specific subclasses such as `Win32Window` and future `iOSWindow` own native window creation,
event pumping, command-console pumping, and native surface handles. `RenderSystem` owns the transition from platform
surface to RHI swapchain. RHI backend headers stay responsible for backend-specific creation details.

The first-stage startup path initializes `EngineRuntime`, creates the main `Window`, then calls
`RenderSystem::InitializeDevice()` and `RenderSystem::CreateMainSwapchain()`. After the main swapchain exists,
`Application` connects `GameThreadSystem` to `RenderSystem`. The Main Thread keeps pumping the `Window`, while the Game
Thread calls `RenderSystem::RenderFrame()` during its render extraction phase. `RenderSystem` records the bound Game
Thread id when `GameThreadSystem` connects it, and Game Thread-only public entry points validate that caller before
submitting work. `RenderSystem` owns a `MaxRenderFramesInFlight` ring of render frame contexts; `RenderFrame()` advances
the frame id, resolves the corresponding ring context, submits one Render Thread command with a `RenderFrameToken`, and
returns. `GameThreadSystem` frame-end sync, not a RenderSystem frame-slot semaphore, controls how far the Game Thread
may run ahead of the Render Thread. Shutdown disconnects `GameThreadSystem`, drains render work, then destroys the main
swapchain and RHI device.

### 9.3 Threading Rule

RHI lifecycle work should execute on the Render Thread.

Public `RenderSystem` lifecycle methods may be called from Main Thread during startup or shutdown. Internally they
should:

1. Validate public state on the caller thread.
2. Submit a private lifecycle command to the Render Thread.
3. Wait for that command to finish.
4. Return success or failure to the caller.

This keeps startup code straightforward while preserving render-thread ownership of live RHI state.

Ordinary `Submit(RenderCommand)` is a Game Thread-only public entry point for render work, resource uploads, and future
render-resource updates. It should not be the primary public API for device or swapchain lifecycle. Main Thread startup
and shutdown paths use explicit lifecycle APIs, which submit private synchronous commands internally.

### 9.4 State Ownership

`RenderSystem` should track render lifecycle state separately from `EngineRuntime` service state:

```text
RenderSystem service state:
  Uninitialized / Initialized / ShuttingDown

RHI device state:
  NoDevice / DeviceReady

Main swapchain state:
  NoSwapchain / SwapchainReady
```

First RHI integration may support one main swapchain for Player. Editor viewport work should avoid assuming there will
only ever be one surface; future Editor support may register additional viewport surfaces or render targets.

### 9.5 Shutdown Order

When RHI ownership is added, `RenderSystem::Shutdown()` should drain normal commands and then perform render-side
teardown in this order:

```text
Stop accepting public commands
Drain accepted commands
Destroy swapchain / viewport surfaces on Render Thread
Destroy remaining RHI state on Render Thread
Destroy RHI device on Render Thread
Join Render Thread
```

## 10. Scene Frame Submission

Milestone 5 adds a higher-level submission boundary for scene rendering:

```text
RenderSystem::SubmitFrame(SceneRenderSnapshot snapshot)
```

The first C++ API follows the current fatal-error RenderSystem style and returns `void`. Submission failure, invalid
thread access, or invalid render state logs a fatal error and terminates rather than returning a status code. The
boundary preserves these rules:

- Game Thread submits immutable render-safe frame packets.
- Frame packets do not contain live `Scene`, `GameObject`, or `Component` pointers.
- Render Thread owns conversion from submitted frame packets into render-side state and RHI commands.
- Game Thread lead is controlled by the existing frame-end `RenderCommandFence`, not by calling `Flush()` every frame.

Ordinary `Submit(RenderCommand)` remains useful for Game Thread initiated uploads and special render work. Scene
rendering should prefer the frame-level API so the engine can reason about queued frames and backpressure.

## 11. Frames In Flight And Backpressure

Game Thread must not run unbounded frames ahead of Render Thread. Milestone 5 uses an `FFrameEndSync`-style CPU
command-stream fence at Game Thread frame end. The initial policy allows one frame of Game Thread lead:

```text
Frame N EndFrame:
  BeginFence(Fence[0])
  Wait(Fence[1])

Frame N + 1 EndFrame:
  BeginFence(Fence[1])
  Wait(Fence[0])
```

The fence signal command executes on the Render Thread after all earlier render commands. Waiting on the previous fence
keeps the Game Thread from completing more than one full frame ahead of the Render Thread while still preserving one
frame of CPU pipeline overlap.

`MaxRenderFramesInFlight = 2` remains the first render frame context ring size. Frame-end sync controls Game Thread
lead over the Render Thread. Render frame contexts organize per-frame render-side data.

The frame slot is a CPU ownership boundary:

- Game Thread owns live Scene mutation.
- Submitted frame slots own immutable extracted scene data or first-stage smoke render work.
- Render Thread owns packet consumption and render-side state updates.

## 12. Render Frames In Flight

Milestone 5 should introduce render-side frame slots before dynamic resources, streaming uploads, or Editor viewport
rendering make lifetime bugs harder to diagnose.

Recommended first value:

```text
MaxRenderFramesInFlight = 2
```

`RenderSystem` owns a fixed set of render frame contexts:

```text
RenderFrameContext[MaxRenderFramesInFlight]
  frameId
  slot derived from (frameId - 1) % MaxRenderFramesInFlight
  slot state: Free / Submitted / Rendering
  command list or backend command allocator equivalent
  transient constant/upload allocation state
  transient descriptor/bind-group allocation state
  deferred render-resource releases
```

The current path allocates one command list per `RenderFrameContext`. `RenderFrame()` and `SubmitFrame()` both advance a
one-based frame id, resolve the corresponding ring slot with `(frameId - 1) % MaxRenderFramesInFlight`, create an
internal `RenderFrameToken` containing the frame id, and submit a Render Thread command that resolves the token back to
the same context before recording the frame.

Keep the three frame concepts distinct:

- Queued Game Thread snapshots protect CPU ownership between Game Thread and Render Thread.
- Render frame contexts organize per-frame render-side data.

## 13. Current Milestone 5 Render Work

Milestone 5 renders scene-driven mesh data through `SceneRenderSnapshot`. The current implementation performs the
minimum render-safe vertical slice:

- Game Thread extracts camera, directional light, mesh renderer, material, transform, and stable object ids.
- Extracted draw items contain transformed clip-space positions and CPU-lit vertex colors.
- Render Thread creates a transient scene vertex buffer from the submitted snapshot and draws it through the existing
  simple forward color pipeline.
- The submitted packet contains no live Scene, GameObject, or Component pointers.

This intentionally avoids expanding the RHI shape in the same step. GPU constant buffers, indexed mesh buffers, depth
attachments, full material binding, shader reflection-driven constants, and persistent render-resource registries remain
future render work.

## 14. Future Render Work

After RHI device and main swapchain lifecycle are connected, later RenderSystem work should add render-resource
registries, upload scheduling, frame begin/end commands, viewport registration, and RenderWorld/RenderScene ownership.
Those features should build on the explicit lifecycle APIs above instead of bypassing them with ad hoc public render
commands.

## 14. Testing Plan

Required tests:

- New `RenderSystem` starts uninitialized.
- `Initialize()` starts the Render Thread.
- Repeated `Initialize()` while running terminates startup.
- Standalone `RenderSystem` can initialize after shutdown.
- Submitted commands execute on the Render Thread.
- `Flush()` waits for commands accepted before the flush call.
- `Shutdown()` drains accepted commands before joining the thread.
- Runtime-owned `RenderSystem` initializes and can execute commands through `EngineRuntime`.
- Milestone 5 frame submission accepts render-safe snapshots without live scene pointers.
- Queued frame backpressure prevents Game Thread submission from running unbounded ahead of Render Thread.
- Render frame contexts are selected through the documented ring mapping.
