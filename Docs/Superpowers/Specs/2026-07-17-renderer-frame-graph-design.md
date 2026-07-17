# Renderer Frame Graph Design

## Purpose

Restructure the render module so renderer-owned code lives under `Engine/Runtime/Render/Renderer`, replace the generic forward renderer with explicit standalone and mobile renderers, execute renderer passes through a compiled frame graph, and classify scene render items through material render queues.

The design follows the build/compile/execute structure, typed resource handles, pass culling, resource versioning, and producer/last-use lifetime tracking described in the referenced FrameGraph article:

- <https://zhuanlan.zhihu.com/p/1997455821202793328>

Unity's render queue model provides the ordering semantics: opaque geometry is rendered before transparent geometry, while transparent geometry is sorted back to front.

## Module Layout

Renderer-owned modules move below a new `Renderer` directory:

```text
Engine/Runtime/Render/
├── Renderer/
│   ├── BaseRenderer.h/.cpp
│   ├── StandaloneRenderer.h/.cpp
│   ├── MobileRenderer.h/.cpp
│   ├── RendererFactory.h/.cpp
│   ├── RenderQueue.h/.cpp
│   ├── FrameGraph/
│   │   ├── FrameGraph.h/.cpp
│   │   ├── FrameGraphBuilder.h/.cpp
│   │   ├── FrameGraphResource.h/.cpp
│   │   └── FrameGraphTransientResourcePool.h/.cpp
│   └── RenderPass/
│       ├── RenderPass.h/.cpp
│       ├── OpaqueSceneRenderPass.h/.cpp
│       └── TransparentSceneRenderPass.h/.cpp
├── RenderFramePipeline.h/.cpp
├── RenderScene.h/.cpp
├── RenderResource.h/.cpp
└── RenderSystem.h/.cpp
```

Editor-owned pass implementations remain under `Editor/RenderPass`, but include and implement the renderer pass interface from the new directory.

`RenderFramePipeline` remains in the render root because it composes whole product frames, including presentation and editor overlays, rather than implementing one scene renderer. Render scene proxies, resource proxies, frame contexts, the render command queue, and `RenderSystem` also remain in the root because their lifetimes and responsibilities are broader than one renderer.

All old `ForwardRenderer` types, fields, and symbols are removed. No aliases or compatibility wrappers remain.

## Renderer Selection

`BaseRenderer` owns the common workflow:

1. Validate frame, scene, and target inputs.
2. Build render queue lists from the current `RTScene` and camera.
3. Create a frame graph for the renderer invocation.
4. Import the renderer color and depth targets.
5. Ask the concrete renderer to register its passes.
6. Compile and execute the graph.

`StandaloneRenderer` is used by Windows and macOS. It registers the opaque and transparent scene passes and accepts additional renderer passes used by the editor, such as the scene grid and gizmo passes.

`MobileRenderer` is used by iOS and future mobile platforms. It independently registers its graph topology. Its initial graph contains the same opaque and transparent scene passes, but it does not accept editor pass injection. Keeping it as a distinct implementation leaves room for tile-memory-aware pass organization without conditional branches inside the standalone renderer.

`RendererFactory` performs compile-time platform selection for player frame pipelines:

- Windows and macOS create `StandaloneRenderer`.
- iOS creates `MobileRenderer`.

Editor frame pipelines explicitly create `StandaloneRenderer` because editor targets are host platforms.

## Frame Graph Phases

Each renderer invocation creates a graph with three distinct phases.

### Build

Passes are registered declaratively. A pass setup callback receives `FrameGraphBuilder` and pass-local data. It declares resource creation, imports, reads, and writes without recording RHI commands. The execute callback receives the immutable pass data and resolved resources after compilation.

Pass implementations expose `AddToFrameGraph` rather than being executed through an external pass vector. For example, `OpaqueSceneRenderPass::AddToFrameGraph` registers an opaque pass node and `TransparentSceneRenderPass::AddToFrameGraph` registers a transparent pass node that consumes the preceding color and depth versions.

### Compile

Compilation performs the following deterministic operations:

1. Validate every resource handle, type, and version.
2. Record each resource version's producing pass and consuming passes.
3. Build read-after-write, write-after-read, and write-after-write dependencies.
4. Mark imported outputs and explicitly side-effecting passes as graph roots.
5. Recursively cull passes and transient resources that cannot contribute to a root.
6. Detect dependency cycles.
7. Produce a stable topological order, using registration order to break otherwise equivalent choices.
8. Record each retained transient resource's producer and final user.

Compile failure returns an error and prevents partial graph execution.

### Execute

Execution walks the compiled order. Before a transient resource's producer executes, the graph acquires the physical resource. After its final user finishes, the graph returns the physical resource to the current frame context's pool. Imported resources are never created, destroyed, or pooled by the graph.

For each raster pass, the graph resolves attachments, begins the RHI render pass, applies viewport and scissor state, invokes the pass execute callback, and ends the RHI render pass. An execution failure propagates through the renderer to `FrameRenderPipeline`.

## Typed Resources and Versioning

The public API uses typed handles:

```cpp
template<typename T>
struct FrameGraphResource
{
    UInt32 index;
    UInt32 version;
};
```

The first implementation supports `FrameGraphTexture`. It defines a texture descriptor and provides access to the resolved `rhi::RhiTexture`. The API remains templated so buffers can be introduced without replacing call sites, but the implementation does not add unused buffer resource machinery.

The builder operations are:

- `Create<T>(name, desc)` declares a graph-owned transient resource.
- `Import<T>(name, desc, resource)` declares an externally owned resource.
- `Read(handle, access)` consumes one exact resource version.
- `Write(handle, access)` consumes the previous version and returns a new version.

Writing always creates a new logical version. This makes ordering explicit when multiple passes load and update the same color target:

```text
ImportedColor v0
  → Opaque writes v1
  → Transparent reads v1 and writes v2
  → Grid reads v2 and writes v3
  → Gizmo reads v3 and writes v4
  → Export v4
```

The graph validates access types at compile time where possible and at graph compilation otherwise. The first texture access categories cover color attachment, depth attachment, and shader read. Automatic backend barriers are outside this change because the common RHI does not yet expose an explicit barrier API; existing backend render-pass and copy paths continue to manage their current state transitions.

## Transient Texture Pool

Each of the two in-flight `FrameContext` instances owns a `FrameGraphTransientResourcePool`. A frame context is reused only after its fence has completed, so resources returned to that context's pool cannot still be in use by the GPU when acquired again.

The pool hashes the full physical texture descriptor, including extent, format, usage, and mip count. Acquisition reuses a matching texture when available or creates one through `RhiDevice`. Release returns it to the matching bucket. `FrameContext::Reset` preserves the pool across frames; `FrameContext::Shutdown` destroys it.

This change does not implement memory aliasing between incompatible resources, background idle-time eviction, or cross-context sharing. Those policies can be added after the graph has real transient consumers and memory telemetry.

## Render Queue Model

The initial queue model uses Unity-compatible numeric anchors while deliberately exposing only the required categories:

```cpp
enum class RenderQueue : UInt16
{
    Opaque = 2000,
    Transparent = 3000,
};
```

Material descriptors accept `"renderQueue": "Opaque"` or `"renderQueue": "Transparent"`. Missing values default to `Opaque`. Invalid strings fail material loading rather than silently changing rendering behavior.

The queue value flows through:

```text
MaterialResource
  → RTMaterialResourceDesc
  → RTMaterialResource
  → RTRenderItem material lookup
  → Renderer render queue lists
```

`BaseRenderer` scans `RTScene` once per renderer invocation and produces two retained item lists in `RendererData`:

- Opaque items use stable scene order in the first implementation.
- Transparent items are sorted back to front using camera position and the render item's world-space bounds center.

Items without a usable material are excluded with an assertion and diagnostic instead of being inserted into an arbitrary queue.

`OpaqueSceneRenderPass` receives only the opaque list. It no longer enumerates `RTScene`. `TransparentSceneRenderPass` receives only the transparent list and uses alpha blending, depth testing, and disabled depth writes. Both passes use the same frame, view, object, and material uniform allocation paths.

## Frame Data Flow

The complete renderer flow is:

```text
RTScene + RTCamera + RendererRenderTarget
  → BaseRenderer::BuildRenderQueues
      → opaque items
      → transparent items
  → ConcreteRenderer::BuildFrameGraph
      → import color/depth targets
      → add opaque pass
      → add transparent pass
      → add optional standalone editor passes
      → export final color version
  → FrameGraph::Compile
      → validate
      → build dependencies
      → cull
      → sort
      → calculate lifetimes
  → FrameGraph::Execute
      → acquire transient resources
      → record retained passes
      → release transient resources
```

Player presentation and the editor ImGui overlay remain in `RenderFramePipeline` outside the renderer graph. The renderer graph owns scene rendering only.

## Failure Handling

- Invalid graph resource handles, versions, or types produce a compile error.
- Cycles produce a compile error naming the involved passes.
- Missing required imported targets fail before command recording.
- Pass execution failure aborts the remaining graph and propagates an error to the frame pipeline.
- Render queue parsing rejects unknown values during material loading.
- A missing camera produces no scene draw passes; the existing frame-pipeline clear/presentation behavior remains responsible for an empty frame.

Assertions provide development diagnostics, but recoverable failures also return an error so release builds do not continue with a partially recorded graph.

## Non-Goals

This change does not add:

- Deferred rendering.
- Post-processing.
- Compute passes or asynchronous compute queues.
- Automatic RHI resource barriers.
- Native render-pass merging.
- Physical-memory aliasing.
- General buffer resources in the frame graph.
- Additional Unity render queue categories such as Background, AlphaTest, or Overlay.

## Verification

Repository policy does not add a new unit-test target for this architecture change. Verification consists of:

- A complete Windows debug build through `CMake/Scripts/WithMsvc.bat`.
- The complete existing Windows CTest preset.
- A source contract check proving that no `ForwardRenderer` symbols or old renderer paths remain.
- A source contract check proving that host and mobile platform selection reaches the correct concrete renderer.
- A source contract check proving that opaque and transparent passes consume their corresponding render queue lists rather than enumerating `RTScene`.
- A source contract check proving that renderer passes are registered through `FrameGraph` and executed from compiled graph order.
- `git diff --check` and a final full-worktree audit.
