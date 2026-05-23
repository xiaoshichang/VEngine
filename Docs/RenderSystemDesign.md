# RenderSystem Design

## 1. Purpose

`RenderSystem` is the runtime service that owns render-thread lifecycle and render command execution. It is managed by
`EngineRuntime` alongside `JobSystem` and `IOSystem`.

The first version intentionally focuses on the runtime boundary:

- Start and stop the Render Thread.
- Accept commands from Game Thread, ResourceSystem, jobs, and tests.
- Execute commands on the Render Thread.
- Provide CPU-side flushing for submitted render commands.
- Leave RHI device, swapchain, render resources, and frame rendering integration for later RHI-focused work.

## 2. Relationship To RHI

`RenderSystem` and RHI documents have different responsibilities.

`RenderSystem` defines:

- Runtime service lifecycle.
- Render Thread ownership.
- Render command queue semantics.
- CPU-side command flushing and shutdown draining.
- How higher-level modules submit work to the Render Thread.

RHI documents define:

- Common graphics backend object model.
- Device, queue, command list, resource, shader, pipeline, binding, and fence concepts.
- D3D11, D3D12, Metal, and future backend mapping.
- GPU submission and resource-state behavior.

This separation keeps the first `RenderSystem` useful before the renderer owns a real swapchain. Later work should attach
RHI device and swapchain ownership to `RenderSystem` without changing the basic service and command-queue contract.

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

## 8. Lifecycle Rules

A standalone `RenderSystem` may be initialized again after `Shutdown()`. This matches `JobSystem` and `IOSystem` and
keeps unit tests straightforward.

`EngineRuntime` remains one-shot. A single `EngineRuntime` object cannot be initialized again after a completed
lifecycle.

Repeated `RenderSystem::Initialize()` while running returns `ErrorCode::InvalidState`.

`Submit()` before initialization, after shutdown, or during shutdown returns `ErrorCode::InvalidState`.

Submitting an empty command function returns `ErrorCode::InvalidArgument`.

## 9. Future RHI Integration

The next RHI-facing step should extend `RenderSystemDesc` with backend and device options:

```text
RenderBackend backend
bool enableDebugDevice
```

Window and swapchain ownership should be connected after Application and platform-window boundaries are explicit:

```text
Application / Platform Window
  -> RenderSystem surface registration
    -> RHI swapchain
```

Editor viewport support should allow more than one render target or swapchain-like output, so the first RHI integration
should avoid hard-coding a single global viewport into the core `RenderSystem` contract.

## 10. Testing Plan

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
