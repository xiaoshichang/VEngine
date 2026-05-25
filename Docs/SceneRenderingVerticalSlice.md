# Scene And Rendering Vertical Slice

## 1. Purpose

This document expands Milestone 5 from `Docs/DevelopmentPlan.md`.

Milestone 5 turns the previous low-level rendering, shader, math, threading, and runtime-service work into the first
real engine scene. The target is a small but complete vertical slice:

```text
Scene
  -> Game Thread update
  -> render-safe snapshot
  -> RenderSystem
  -> Render Thread
  -> RHI
  -> swapchain present
```

The milestone should prove that VEngine can load or construct a simple scene containing a camera, directional light,
transform hierarchy, and one static mesh, then render it without letting Game Thread code call RHI directly.

## 2. First-Stage Non-Goals

This milestone should stay narrow. It does not include:

- Editor scene editing.
- Prefab workflows.
- Full source asset import.
- Hot reload.
- Skeletal animation.
- PBR materials.
- Shadows.
- Post-processing.
- Runtime scripting.
- Physics or picking.
- Multiple game worlds.
- Multiple Editor viewports.

These features should remain future work unless a small placeholder is required to keep the first vertical slice
extensible.

## 3. System Ownership

Milestone 5 introduces the first explicit Game Thread owner:

```text
EngineRuntime
  JobSystem
  IOSystem
  ResourceManager
  RenderSystem
  GameThreadSystem
```

`GameThreadSystem` owns the Game Thread execution context, frame counter, tick phases, and the rule that live scene
mutation happens on the Game Thread. It does not own the platform message pump and it does not own RHI state.

`Application` continues to own platform startup, logging setup, the main window, and platform message pumping. The first
Milestone 5 implementation creates a dedicated physical Game Thread owned by `GameThreadSystem`:

```text
Application::Run()
  Pump platform messages
  Build input snapshot
  Publish input/lifecycle commands

GameThreadSystem dedicated thread
  Run frame loop
```

`Application` and Main Thread code should still communicate with scene update work through `GameThreadSystem`. Input,
lifecycle, and Editor commands should cross this boundary through explicit frame data or thread-safe queues rather than
directly mutating live scene state.

`RenderSystem` owns the Render Thread, render command consumption, render-side resource state, RHI device/swapchain
lifecycle, and render-side in-flight frame slots.

`ResourceManager` owns runtime resource handles, resource lookup, fallback resources, and lifetime tracking. It may use
`IOSystem` and `JobSystem`, but the point where loaded data enters live scene state is owned by the Game Thread.

## 4. Initialization And Shutdown Order

Recommended Milestone 5 initialization order:

```text
Application startup
  -> Logging
  -> Platform window
  -> EngineRuntime
      -> JobSystem
      -> IOSystem
      -> ResourceManager
      -> RenderSystem
      -> GameThreadSystem
  -> RenderSystem device and main swapchain
  -> Load or construct sample scene
  -> Main loop
```

`GameThreadSystem` should start ticking only after `ResourceManager` and `RenderSystem` are available. The current
Player path creates the RHI device and main swapchain, connects `GameThreadSystem` to `RenderSystem`, constructs the
sample scene in code, and then assigns it as the active scene. Until an active scene is bound, the Game Thread can run
empty frames or the existing clear/triangle smoke path.

Recommended shutdown order:

```text
Stop accepting new platform/game commands
Stop GameThreadSystem ticking
Submit final render cleanup commands
Flush RenderSystem CPU commands
Destroy active Scene
Release ResourceManager runtime resources
Destroy swapchain and RHI device through RenderSystem
EngineRuntime::Shutdown()
```

`RenderSystem::Flush()` remains a CPU command-queue fence for Render Thread work.

## 5. Game Thread Design

### 5.1 Responsibilities

`GameThreadSystem` is responsible for:

- Owning the current Game Thread identity.
- Driving frame begin, update, late update, render extraction, and frame end phases.
- Owning the authoritative mutation point for live `Scene`, `GameObject`, and `Component` state.
- Applying input snapshots produced by the platform/application layer.
- Draining resource and IO completions at a controlled frame boundary.
- Dispatching component lifecycle calls.
- Producing render-safe scene snapshots for `RenderSystem`.
- Applying backpressure when Render Thread work gets too far behind.

It is not responsible for:

- Pumping OS platform messages.
- Creating RHI devices, swapchains, or command lists.
- Mutating render-side resource objects directly.
- Performing blocking file reads on the frame path when an async route exists.

### 5.2 First Tick Shape

The first version should keep the frame shape explicit and easy to inspect:

```text
GameThreadSystem::RunLoop()
  while running:
    BeginFrame
      Update frame id
      Time::Tick()
      Consume input snapshot
      Drain completed IO/resource work

    SceneLifecycle
      Dispatch pending OnCreate / OnDestroy
      Dispatch pending OnEnable / OnDisable

    Update
      Call OnUpdate on enabled components

    LateUpdate
      Call OnLateUpdate on enabled components

    TransformUpdate
      Resolve dirty TransformComponent hierarchy

    RenderExtraction
      Build SceneRenderSnapshot
      Synchronize ResourceManager changes into RenderSystem
      Submit snapshot to RenderSystem
      If no active scene exists, drive RenderSystem::RenderFrame()

    EndFrame
      Retire released transient game-frame data
```

The exact function names may change during implementation, but the phase order should stay documented and covered by
tests where practical.

The current vertical-slice bridge connects `GameThreadSystem` to `RenderSystem` after the main swapchain is created.
During `RenderExtraction`, the Game Thread extracts a `SceneRenderSnapshot`, calls
`RenderSystem::SynchronizeRenderResources()` to enqueue mesh/material registry changes when resource revisions changed,
then calls `RenderSystem::SubmitFrame()` for the immutable frame packet. If there is no active scene, the fallback path
still calls `RenderSystem::RenderFrame()` to keep the swapchain smoke path alive. `RenderSystem` owns
`MaxRenderFramesInFlight` render frame contexts; `RenderFrame()` and `SubmitFrame()` advance the frame id, submit one
frame command with a `RenderFrameToken` to the Render Thread, and return once that command is accepted. The Render
Thread uses the token to resolve the matching `RenderFrameContext`. `GameThreadSystem` frame-end sync controls how far
the Game Thread may run ahead of the Render Thread. `RenderSystem` records the bound Game Thread id when
`GameThreadSystem` connects it, and `RenderFrame()`, `SubmitFrame()`,
`SynchronizeRenderResources()`, and ordinary `Submit(RenderCommand)` validate that caller before accepting Game Thread
initiated work. Submission or frame execution failures are fatal errors.

### 5.3 Main Thread Relationship

The Main Thread owns platform events. The Game Thread owns scene mutation.

The first Milestone 5 implementation uses a dedicated physical Game Thread. The important rule is that platform code
hands data to the game update through explicit snapshots or commands:

```text
Platform input events
  -> InputSnapshot
  -> GameThreadSystem command/input boundary
```

The same boundary becomes a real command queue as input, lifecycle, and Editor work are added:

```text
Main Thread
  Platform events
  Submit input/lifecycle/editor commands

Game Thread
  Consume commands at frame boundary
  Mutate Scene
```

Editor operations in Milestone 7 should follow the same model. The Editor UI may run on the Main Thread, but scene
edits should be represented as Game Thread commands instead of directly mutating live scene objects from UI code.

### 5.4 Scene Mutation Rules

Live scene mutation is allowed only on the Game Thread unless a future API explicitly documents a synchronized mutation
point.

Rules:

- `Scene`, `GameObject`, and `Component` mutation APIs should assert or validate Game Thread ownership in debug builds.
- Worker jobs may prepare data, but they must not directly mutate the GameObject hierarchy.
- IO completions should publish loaded bytes or decoded intermediate data; Game Thread code decides when that data
  becomes live resource or scene state.
- Render Thread never receives live `GameObject`, `Component`, or `Scene` pointers.
- Render Thread receives render snapshots, render resource handles, or render commands whose captured data is valid
  until the command or frame packet is consumed.

## 6. Scene Model Work

Add `Scene` as the owner of GameObject storage, root objects, update traversal, component lifecycle dispatch, and object
lookup.

Add `GameObject` with:

- Stable object identifier.
- Name.
- Active state.
- Parent-child hierarchy.
- Component list.
- Scene ownership.

Add `Component` with:

- Owner `GameObject` access.
- Enabled state.
- Runtime type information.
- Lifecycle hooks.

Initial lifecycle hooks:

```text
OnCreate
OnDestroy
OnEnable
OnDisable
OnUpdate
OnLateUpdate
```

Implement `TransformComponent` with local position, rotation, scale, parent relation, local matrix, world matrix, and
dirty propagation through the hierarchy.

## 7. First Render-Facing Components

Implement `CameraComponent` with:

- Projection mode.
- Field of view.
- Orthographic size.
- Near and far planes.
- Viewport rect.
- Clear color.
- View matrix extraction.
- Projection matrix extraction.

Implement `MeshRendererComponent` with:

- Mesh resource handle.
- Material resource handle.
- Visibility flag.
- Local bounds placeholder.

Implement `LightComponent` with directional light support first:

- Light type.
- Color.
- Intensity.
- Direction derived from the GameObject transform.

Component data should stay independent from backend-specific RHI objects. Components should reference engine resource
handles, not D3D or Metal objects.

## 8. Reflection Work

Add a minimal reflection registry for engine types and component types.

Required first-stage metadata:

- Type name.
- Base type relation.
- Component factory function.
- Property name.
- Property type.
- Property offset or accessor.
- Default value.
- Serialization visibility flag.
- Editor visibility flag.
- Enum value metadata.

Reflection should support component creation by type name, property lookup, serialization, and future Inspector editing.
Registration can be explicit in this milestone. Code generation is not part of this milestone.

## 9. Scene Serialization Work

Define the first JSON scene serialization shape around:

- Scene metadata.
- GameObjects.
- Object identifiers.
- Names.
- Active states.
- Parent-child relationships.
- Component lists.
- Reflected component property values.

Serialize component properties through reflection rather than component-specific ad hoc writers where practical.

Missing or unknown component types should produce a logged warning and be skipped gracefully so scenes remain
diagnosable while the engine is evolving.

The asset-level `.vescene` file contract and sample scene assets remain Milestone 6 work. Milestone 5 tests exercise
the JSON serialization shape in memory.

## 10. Resource Work

Add a typed resource handle shape such as:

```text
ResourceHandle<MeshResource>
ResourceHandle<MaterialResource>
ResourceHandle<TextureResource>
ResourceHandle<ShaderResource>
```

Add a first `ResourceManager` responsible for:

- Runtime resource lookup.
- Fallback resource creation.
- Lifetime tracking.
- Missing-resource behavior.
- Mesh and material resources needed by the demo scene.

Full source asset import remains Milestone 6 work. Milestone 5 uses generated built-in resources rather than file-backed
resource loading.

Required fallback resources:

- Cube mesh.
- Default material.

Texture and shader resource management remain placeholders for the asset/material expansion. The current shader path is
owned by the RenderSystem/RHI vertical slice, not by `ResourceManager`.

## 11. Scene-To-Render Synchronization

### 11.1 Render Snapshot Contract

Game Thread extraction converts live scene state into an immutable render packet:

```text
SceneRenderSnapshot
  frameId
  mainCamera
  directionalLights
  drawItems
```

Each draw item should contain render-safe data:

```text
DrawItem
  object id
  worldTransform
  mesh handle
  material handle
  bounds placeholder
```

Resource upload data is not embedded in the snapshot. `RenderSystem::SynchronizeRenderResources()` compares
`ResourceManager` revisions against the Game Thread's submitted mirror, builds add/update/remove render-resource
registry commands only when resource state changes, and draw items reference those persistent render resources by handle
rather than embedding duplicate vertex or material data.

The snapshot must not contain live `GameObject`, `Component`, or mutable `Scene` pointers. It may contain stable
resource handles if `RenderSystem` and `ResourceManager` agree on their lifetime rules.

### 11.2 Submission Boundary

The preferred public shape is a higher-level frame submission API on `RenderSystem`, not an arbitrary lambda from scene
code:

```text
RenderSystem::SubmitFrame(SceneRenderSnapshot snapshot)
```

The implementation may internally translate this into render commands, but the public boundary should communicate that
the Game Thread is submitting a frame packet, not directly editing render internals.

Resource lifetime is synchronized separately from frame submission. The Game Thread asks `RenderSystem` to compare
`ResourceManager` revisions against its submitted mirror and enqueue render-resource registry commands only for changed
resources:

```text
RenderSystem::SynchronizeRenderResources(const ResourceManager& resourceManager)
```

The first implementation follows the current RenderSystem fatal-error style:

```text
void RenderSystem::SubmitFrame(SceneRenderSnapshot snapshot)
void RenderSystem::SynchronizeRenderResources(const ResourceManager& resourceManager)
```

Invalid state, invalid thread access, and queue submission failures log a fatal error and terminate. Internally the
RenderSystem still creates a frame token for the selected render-frame context. The Game Thread learns pacing through
frame-end sync rather than a public status value. The implementation does not call `RenderSystem::Flush()` every frame;
that would hide the real asynchronous contract.

### 11.3 Backpressure

The Game Thread must not run unbounded frames ahead of the Render Thread.

Milestone 5 should use an `FFrameEndSync`-style frame-end synchronizer to control that lead. The Game Thread inserts a
CPU `RenderCommandFence` into the Render Thread queue at `EndFrame`. With one-frame thread lag enabled, it then waits
on the previous fence rather than the one it just inserted:

```text
Frame N EndFrame:
  BeginFence(Fence[0])
  Wait(Fence[1])

Frame N + 1 EndFrame:
  BeginFence(Fence[1])
  Wait(Fence[0])
```

This allows one frame of CPU pipeline overlap while preventing the Game Thread from completing more than one full frame
ahead of the Render Thread. Future Editor viewport updates may choose to coalesce visual-only frames, but scene mutation
and gameplay frames should not be silently dropped.

This is CPU-side backpressure between Game Thread and Render Thread. `MaxRenderFramesInFlight = 2` remains the initial
render-side context ring size, but the frame-end synchronizer is the mechanism that controls Game Thread lead over the
Render Thread.

## 12. Render Thread In-Flight Frames

Milestone 5 should introduce render-side frame slots even if the first D3D11 path can complete quickly.

Recommended first value:

```text
MaxRenderFramesInFlight = 2
```

`RenderSystem` owns a fixed array of render frame contexts:

```text
RenderFrameContext[MaxRenderFramesInFlight]
  frameId
  slot derived from (frameId - 1) % MaxRenderFramesInFlight
  slot state: Free / Submitted / Rendering
  command list or backend command allocator equivalent
```

The current vertical slice owns command lists and frame slot state. Transient constant/upload allocators,
descriptor/bind-group allocation state, and deferred render-resource release queues are future extensions.

Render Thread in-flight frames and Game Thread queued snapshots are related but not the same thing:

- Queued snapshots protect CPU ownership between Game Thread and Render Thread.
- Render frame contexts organize per-frame render-side data.

## 13. Render Integration Work

Add a first render-side representation owned by the Render Thread. The current vertical slice uses a narrow persistent
mesh/material resource registry; a fuller RenderScene or RenderWorld can grow from this once more draw state and
viewports exist.

Required first render work:

- Convert `SceneRenderSnapshot` into render-side camera, light, draw, material, and transform state.
- Create render resources for static mesh vertex buffers.
- Create basic material state.
- Create shader pipeline objects for the first forward color pipeline.
- Render one active camera.
- Render one directional light.
- Render one or more static mesh draw calls.
- Present through the main swapchain.

Indexed buffers, depth attachments, offline shader artifact loading, and full shader reflection-driven binding are
deferred to the next RHI/render-resource expansion.

The first implementation should use one main viewport, one active scene camera, a fixed forward pass, and a default
material path.

Current Milestone 5 implementation note:

- `SceneRenderSnapshot` is already the Game-to-Render boundary.
- The first scene render path extracts draw items as render mesh/material handles plus camera matrices, world
  transforms, and directional light data without pre-projecting vertices on the Game Thread.
- Mesh vertices, normals, colors, and material constants remain owned by `ResourceManager`. Resource create/update/remove
  changes advance resource revisions and a resource change serial.
- `RenderSystem::SynchronizeRenderResources()` builds add/update/remove render commands only when that serial changes.
  The Render Thread applies those commands to its persistent render-resource registry.
- Frame submission no longer carries mesh/material payloads. The Render Thread binds persistent scene mesh vertex buffers
  from the registry and per-draw uniform buffers. The vertex shader applies the conventional `projection * view * world`
  MVP transform.
- Indexed drawing, depth buffer attachment, full material binding, shader reflection-driven constants, and GPU-safe
  deferred resource release remain future render work.

## 14. EngineRuntime Integration Work

Connect Milestone 5 systems through `EngineRuntime` without global singletons.

Required access:

- `GetJobSystem()`.
- `GetIOSystem()`.
- `GetResourceManager()`.
- `GetRenderSystem()`.
- `GetGameThreadSystem()`.

The exact API can evolve, but Player and Editor should share the same runtime service boundary.

Service initialization failures inside `EngineRuntime` remain unrecoverable startup failures. They should be logged as
fatal errors and terminate startup rather than returning partially initialized runtime state.

## 15. Tests And Smoke Checks

Unit tests:

- GameObject creation.
- Parent-child hierarchy.
- Component add/remove.
- Active and enabled behavior.
- Lifecycle dispatch order.
- Transform local/world matrix updates.
- Transform dirty propagation.
- Reflection component registration.
- Reflection factory creation.
- Reflection property lookup.
- Enum metadata lookup.
- Reflected default values.
- Scene serialization round trip.
- Fallback resource lookup.
- Missing-resource behavior.

Integration or smoke checks:

- Construct the sample scene in Windows Player.
- Submit a render snapshot without live scene pointers.
- Open Windows Player.
- Render a lit static mesh.
- Exit cleanly.

The current automated tests cover scene construction, transform propagation, reflection-backed serialization round trip,
fallback resource lookup, resource create/update/destroy revision behavior, render snapshot extraction, Game Thread
frame ownership, and ShaderTool smoke coverage. Windows Player constructs the sample scene and binds it to the Game
Thread; manual Player execution remains the smoke check for the visible window path until an automated window
capture/exit harness lands. Dedicated RenderSystem tests for frame submission, frame slot selection, backpressure, and
render-resource synchronization remain pending.

## 16. Milestone Exit Criteria

Milestone 5 is complete when:

- `VEnginePlayer` constructs the sample scene and renders a static mesh from a camera with a directional light.
- The rendered frame uses the `RenderSystem` and RHI path established by earlier milestones.
- Scene serialization can save and reload the sample scene without losing hierarchy or component property data.
- Scene mutation APIs are Game Thread-owned.
- Render Thread does not directly access live `GameObject`, `Component`, or `Scene` state.
- Game Thread submission to Render Thread uses render snapshots or equivalent render-safe frame packets.
- Render-side frame slots and completion tracking exist for at least two frames in flight.
- Core Scene, Reflection, Resource, Game Thread, and serialization tests are registered with CTest and pass on Windows
  through the MSVC preset.

Current implementation caveat: matrix constants travel through a narrow RHI uniform-buffer path, and mesh/material data
is cached render-side by resource id with resource revisions. Indexed buffers, depth, full material binding layouts,
upload scheduling, and GPU-safe deferred release require the next RHI/render-resource expansion.
