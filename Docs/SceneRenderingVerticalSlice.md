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

`GameThreadSystem` should start ticking only after `ResourceManager` and `RenderSystem` are available. The active scene
may be loaded by `GameThreadSystem` or assigned to it by a higher-level runtime API, but scene activation must happen
before the first gameplay tick.

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

Render-side GPU fences are still RHI concepts. `RenderSystem::Flush()` remains a CPU command-queue fence and should not
silently become a GPU-idle operation.

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
- Applying backpressure when Render Thread or GPU work gets too far behind.

It is not responsible for:

- Pumping OS platform messages.
- Creating RHI devices, swapchains, command lists, or GPU fences.
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
      Current smoke path: drive RenderSystem::RenderFrame()
      Target scene path: submit snapshot to RenderSystem

    EndFrame
      Retire released transient game-frame data
```

The exact function names may change during implementation, but the phase order should stay documented and covered by
tests where practical.

The current vertical-slice bridge connects `GameThreadSystem` to `RenderSystem` after the main swapchain is created.
During `RenderExtraction`, the Game Thread calls `RenderSystem::RenderFrame()`. `RenderSystem` owns
`MaxRenderFramesInFlight` slots; `RenderFrame()` blocks only when all slots are already in flight, submits one frame
command with a `RenderFrameToken` to the Render Thread, and returns once that command is accepted. The Render Thread
uses the token to resolve the matching `RenderFrameContext`. `RenderSystem` records the bound Game Thread id when
`GameThreadSystem` connects it, and `RenderFrame()`/ordinary `Submit(RenderCommand)` validate that caller before
accepting Game Thread initiated work. Submission or frame execution failures are fatal errors. When the scene snapshot
path lands, this narrow boundary should become `RenderSystem::SubmitFrame()` with the same bounded in-flight-frame
rule.

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

Define the first `.vescene` structure around:

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

Add a small sample scene under sample assets once the format is stable enough for tests.

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
- Mesh, material, texture, and shader resources needed by the demo scene.

Full source asset import remains Milestone 6 work. Milestone 5 may use simple engine-native sample files or generated
built-in resources.

Required fallback resources:

- Cube or triangle mesh.
- Default material.
- Default shader references.
- Placeholder texture if texture binding is already required by the RHI path.

Resource loading should route through FileSystem and IOSystem boundaries where practical. Synchronous loading is allowed
for the first vertical slice if the async path would expand the milestone too far.

## 11. Scene-To-Render Synchronization

### 11.1 Render Snapshot Contract

Game Thread extraction converts live scene state into an immutable render packet:

```text
SceneRenderSnapshot
  frameId
  mainCamera
  directionalLights
  drawItems
  referencedRenderResources
```

Each draw item should contain render-safe data:

```text
DrawItem
  worldTransform
  mesh handle or render mesh handle
  material handle or render material handle
  bounds placeholder
  visibility flags
```

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

`SubmitFrame()` should return a token or status that allows the Game Thread to know whether the frame packet was
accepted:

```text
RenderFrameToken
  frameId
  submissionIndex
```

The first implementation may block at the frame boundary if the render submission queue is full. It should not call
`RenderSystem::Flush()` every frame just to stay simple; that would hide the real asynchronous contract.

### 11.3 Backpressure

The Game Thread must not run unbounded frames ahead of the Render Thread.

Milestone 5 should define a small limit:

```text
MaxRenderFramesInFlight = 2
```

When this limit is reached, Player should block at the next render submission boundary until the Render Thread completes
at least one submitted frame and returns a slot to the frame-slot semaphore. Future Editor viewport updates may choose to coalesce
visual-only frames, but scene mutation and gameplay frames should not be silently dropped.

This is CPU-side backpressure. GPU frame latency is tracked separately by RHI fences or backend completion primitives.

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
  transient constant/upload allocation state
  transient descriptor/bind-group allocation state
  deferred render-resource releases
  RHI fence or backend completion value
```

Before the Render Thread reuses a frame slot, it must ensure the GPU has completed work that still references resources
from that slot. The exact backend mapping belongs to RHI:

```text
D3D12
  ID3D12Fence value per submitted frame.

Metal
  MTLCommandBuffer completion handler or shared event when needed.

D3D11
  DXGI frame latency object, query, or a CPU-side compatibility fence adapter.
```

D3D11 may initially behave as if work completes immediately after present if that is the only implemented path, but the
common render-side lifetime model should still keep frame slots and completion points visible so D3D12 and Metal do not
require a redesign.

Render Thread in-flight frames and Game Thread queued snapshots are related but not the same thing:

- Queued snapshots protect CPU ownership between Game Thread and Render Thread.
- Render frame slots protect transient render resources between Render Thread and GPU.
- RHI fences protect GPU lifetime and frame-slot reuse.

## 13. Render Integration Work

Add a first render-scene or render-world representation owned by the render side of the engine.

Required work:

- Convert `SceneRenderSnapshot` into render-side camera, light, draw, material, and transform state.
- Create render resources for static mesh vertex and index buffers.
- Create basic material state.
- Create shader pipeline objects from Milestone 4 shader outputs.
- Add a depth buffer.
- Render one active camera.
- Render one directional light.
- Render one or more static mesh draw calls.
- Present through the main swapchain.

The first implementation should use one main viewport, one active scene camera, a fixed forward pass, and a default
material path.

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

- Load or construct the sample scene.
- Submit a render snapshot without live scene pointers.
- Open Windows Player.
- Render a lit static mesh.
- Exit cleanly.

## 16. Milestone Exit Criteria

Milestone 5 is complete when:

- `VEnginePlayer` can load or construct the sample scene and render a static mesh from a camera with a directional
  light.
- The rendered frame uses the `RenderSystem` and RHI path established by earlier milestones.
- Scene serialization can save and reload the sample scene without losing hierarchy or component property data.
- Scene mutation APIs are Game Thread-owned.
- Render Thread does not directly access live `GameObject`, `Component`, or `Scene` state.
- Game Thread submission to Render Thread uses render snapshots or equivalent render-safe frame packets.
- Render-side frame slots and completion tracking exist for at least two frames in flight.
- Core Scene, Reflection, Resource, Game Thread, and serialization tests are registered with CTest and pass on Windows
  through the MSVC preset.
