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
- Provide a lightweight pass orchestration shape through `FrameRenderer` and `RenderPass`.
- Leave RenderWorld, render-resource registries, upload scheduling, full frame graph dependency analysis, and viewport
  registration for later renderer work.

## 1.1 Render Resource Ownership Model

VEngine should follow an Unreal-style split between Scene Thread ownership and Render Thread ownership for render
resources.

The Scene Thread owns CPU-side render descriptions and render-facing gameplay state. The Render Thread owns the
matching backend proxy objects.

Recommended naming:

- Scene Thread CPU object: `RenderTarget`
- Render Thread proxy object: `RTRenderTarget`
- Any Render Thread proxy class should use an `RT` prefix to make the ownership boundary obvious in code review and
  debugging.

Ownership rules:

- Scene Thread objects describe logical render state such as binding kind, extent, format, and editor-facing metadata.
- RT proxy objects own live RHI resources and any render-thread-only handles.
- Scene Thread objects may hold `std::shared_ptr` to their RT proxy so render commands already in flight can keep the RT
  proxy alive after the CPU object is destroyed.
- RT proxy destruction must be safe after the Scene Thread object has been released.
- Scene Thread code must not call RHI directly when it needs to create or update a render resource.
- Scene Thread code should instead use an `InitRenderResource`-style API that submits work through the
  `RenderSystem` render command queue.

Suggested lifecycle:

```text
Scene Thread
  Construct RenderTarget
  Initialize CPU-side fields
  Call InitRenderResource(RenderSystem&)
  Update logical state through scene events
  Release RenderTarget when scene data no longer needs it

Render Thread
  Create RTRenderTarget
  Build or refresh RHI resources
  Consume render commands that captured shared_ptr<RTRenderTarget>
  Release RHI resources when the last RT reference goes away
```

This model keeps render-side lifetime explicit while allowing the Scene Thread to own ordinary engine objects. It also
fits the later viewport merge flow: camera data, viewport description, and RT proxy references can be combined when the
render command is assembled, rather than binding camera objects directly to platform surfaces.

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
- Device, queue, command list, resource, shader, pipeline, binding, and fence concepts.
- D3D11, D3D12, Metal, and future backend mapping.
- GPU submission and resource-state behavior.

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
- `RenderCommand`.
- One Render Thread owned by `RenderSystem`.
- A lock-free multi-producer, single-consumer render command queue.
- `Submit()` for render commands.
- `Flush()` for CPU-side command completion.
- Integration into `EngineRuntime`.
- RHI device lifecycle APIs.
- Main swapchain lifecycle APIs.
- A minimal `FrameRenderer` and `RenderPass` path that clears, draws one triangle, submits, and presents.
- Unit tests for lifecycle, command execution, flushing, shutdown draining, and runtime access.

The first version does not create or own:

- RHI device.
- Swapchain.
- Platform window or Metal layer.
- Render resources.
- RenderWorld or RenderScene.
- GPU fences.
- Frame graph.
- Mesh, material, texture, or UI rendering.

## 5. Frame Renderer And Render Pass Model

`FrameRenderer` is the first renderer-level frame orchestrator. It owns long-lived `RenderPass` objects, then builds
per-frame pass data from the current main-swapchain frame context.

First-stage frame flow:

```text
FrameRenderer
  Build frame context
  Update render world
  Build visible draw lists
  Execute passes in order
```

The current implementation keeps `UpdateRenderWorld()` and `BuildVisibleDrawLists()` as explicit no-op extension points
until render proxies and mesh draw lists exist. This keeps the threading boundary visible without introducing a partial
RenderWorld.

Renderer-level passes use this interface:

```cpp
class RenderPass
{
public:
    virtual const char* GetName() const noexcept = 0;
    virtual void Setup(RenderPassBuilder& builder) = 0;
    virtual void Execute(RenderPassContext& context) = 0;
};
```

`Setup()` runs every frame and declares the pass attachment shape through `RenderPassBuilder`. `Execute()` records draw
commands through `RenderPassContext` after the RHI render pass has begun. The pass object itself is long-lived; the pass
descriptor, viewport, scissor, and frame context are rebuilt per frame.

The first concrete pass is the triangle smoke pass. It replaces the earlier direct `RenderSystem::RenderFrame()` draw
logic with this shape:

```text
TriangleForwardPass
  Setup:
    write swapchain color attachment
    clear -> store

  Execute:
    bind triangle pipeline
    bind triangle vertex buffer
    draw 3 vertices
```

This is intentionally not a full render graph. There is no automatic pass culling, transient resource aliasing, resource
state inference, or graph-level scheduling yet. The current goal is to make the frame/pass ownership boundary real while
keeping the first rendering vertical slice small.

## 6. Command Queue Model

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

`debugName` is diagnostic and may be empty. `function` must be callable as `void()`.

Captured data must remain valid until the command runs. Future render-resource handles should replace raw captured
engine pointers where possible.

## 7. Render Command Execution

Render commands execute on the Render Thread without a per-command context object:

```cpp
using RenderCommandFunction = std::function<void()>;
```

`RenderSystem` exposes the last known Render Thread id through `GetRenderThreadId()` for diagnostics and tests. RHI
device access, frame state, upload helpers, and render-resource registries should be exposed through explicit
RenderSystem or renderer-owned APIs rather than a generic command context.

When a Scene Thread object needs to touch RHI-backed state, it should expose a dedicated initialization or update method
that submits a render command and forwards the RT proxy:

```cpp
void RenderTarget::InitRenderResource(RenderSystem& renderSystem)
{
    EnsureRenderThreadProxy();
    RenderTargetDesc desc = BuildCurrentDesc();
    renderSystem.InitRenderResource(rtRenderTarget_, std::move(desc));
}
```

`RenderSystem::InitRenderResource()` copies the CPU-side description when the command is submitted. The Render Thread
then updates `RTRenderTarget` and creates or replaces the RHI texture from that snapshot. The exact public API can
change, but the rule should stay stable: RHI-facing work travels through render commands, not through direct Scene
Thread calls or shared mutable CPU descriptions.

## 8. Flush And Shutdown Semantics

`Flush()` waits until all commands submitted before the flush call have completed on the Render Thread.

`Flush()` is a CPU render command queue fence. It does not mean:

- GPU idle.
- RHI queue idle.
- swapchain presented.
- resource uploads are visible to shaders.

Shutdown behavior:

1. Stop accepting new commands.
2. Wake the Render Thread.
3. Drain commands that were accepted before shutdown.
4. Join the Render Thread.
5. Clear runtime state.

Commands submitted after shutdown starts fail with `ErrorCode::InvalidState`.

## 9. Lifecycle Rules

A standalone `RenderSystem` may be initialized again after `Shutdown()`. This matches `JobSystem` and `IOSystem` and
keeps unit tests straightforward.

`EngineRuntime` remains one-shot. A single `EngineRuntime` object cannot be initialized again after a completed
lifecycle.

Repeated `RenderSystem::Initialize()` while running returns `ErrorCode::InvalidState`.

`Submit()` before initialization, after shutdown, or during shutdown returns `ErrorCode::InvalidState`.

Submitting an empty command function returns `ErrorCode::InvalidArgument`.

## 10. RHI Device And Swapchain Lifecycle

### 10.1 Reference Direction

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

### 10.2 Public Lifecycle API

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
RenderBackend GetDeviceBackend() const noexcept;
ErrorCode RenderFrame();
```

`Application` obtains native surface information through the `Window` base interface instead of branching on a platform
macro itself. Platform-specific subclasses such as `Win32Window` and future `iOSWindow` own native window creation,
event pumping, command-console pumping, and native surface handles. `RenderSystem` owns the transition from platform
surface to RHI swapchain. RHI backend headers stay responsible for backend-specific creation details.

The first-stage startup path initializes `EngineRuntime`, creates the main `Window`, then calls
`RenderSystem::InitializeDevice()` and `RenderSystem::CreateMainSwapchain()` before entering the main loop. Each loop
iteration pumps the `Window`, begins the main render frame, executes renderer passes such as `TriangleForwardPass`,
submits, and presents. Shutdown destroys the main swapchain and RHI device before `EngineRuntime::Shutdown()`.

### 10.3 Threading Rule

RHI lifecycle work should execute on the Render Thread.

Public `RenderSystem` lifecycle methods may be called from Main Thread during startup or shutdown. Internally they
should:

1. Validate public state on the caller thread.
2. Submit a private lifecycle command to the Render Thread.
3. Wait for that command to finish.
4. Return success or failure to the caller.

This keeps startup code straightforward while preserving render-thread ownership of live RHI state.

Ordinary `Submit(RenderCommand)` remains available for render work, resource uploads, and future render-resource
updates. It should not be the primary public API for device or swapchain lifecycle.

### 10.4 State Ownership

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

### 10.5 Shutdown Order

When RHI ownership is added, `RenderSystem::Shutdown()` should drain normal commands and then perform render-side
teardown in this order:

```text
Stop accepting public commands
Drain accepted commands
Destroy swapchain / viewport surfaces on Render Thread
Flush or wait-idle RHI device as needed
Destroy RHI device on Render Thread
Join Render Thread
```

GPU fences and frame latency should remain RHI concepts. `RenderSystem` coordinates when to call them during lifecycle
transitions, but should not turn CPU command `Flush()` into a GPU-idle operation.

## 11. Future Render Work

After RHI device and main swapchain lifecycle are connected, later RenderSystem work should add render-resource
registries, upload scheduling, frame begin/end commands, viewport registration, and RenderWorld/RenderScene ownership.
Those features should build on the explicit lifecycle APIs above instead of bypassing them with ad hoc public render
commands.

The first resource registry should distinguish:

- CPU-side resource records owned by Scene Thread or ResourceSystem.
- RT proxy objects owned by Render Thread.
- Shared ownership bridges used only to keep RT proxies alive long enough for queued render commands to complete.

That separation is especially important for viewport and render-target objects, because a scene or editor tab may drop
its CPU-side object while the Render Thread is still draining the previous frame.

## 12. Testing Plan

Required tests:

- New `RenderSystem` starts uninitialized.
- `Initialize()` starts the Render Thread.
- Repeated `Initialize()` while running fails.
- Standalone `RenderSystem` can initialize after shutdown.
- `Submit()` before initialization and after shutdown fails.
- Submitted commands execute on the Render Thread.
- `Flush()` waits for commands accepted before the flush call.
- `Shutdown()` drains accepted commands before joining the thread.
- Runtime-owned `RenderSystem` initializes and can execute commands through `EngineRuntime`.
