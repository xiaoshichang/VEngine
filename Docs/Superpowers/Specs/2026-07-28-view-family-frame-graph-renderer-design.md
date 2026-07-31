# View-Family Renderer And Frame-Graph VSM Design

## Goal

Refactor the render architecture so one frame-local renderer consumes one `RenderViewFamily` and owns the frame's single
`FrameGraph`. Move Virtual Shadow Map GPU scheduling into small frame-graph passes whose declared resource accesses,
rather than manually written loops, determine execution order. Remove routine `ErrorCode` propagation from renderer
internals and terminate on rendering, RHI, swapchain, submission, or presentation failure.

This is a one-time migration. The old per-view renderer path and `PreRenderShadowStep` recording path will not remain as
compatibility alternatives.

## Architectural Layers And Lifetimes

```text
RenderSystem (long-lived)
  -> VirtualShadowManager
  -> ShaderManager
  -> FrameContext and transient resource pools
  -> uniform pools and other cross-frame acceleration/cache structures

FrameRenderPipeline (one frame)
  -> EditorRenderFramePipeline or PlayerRenderFramePipeline
  -> selects and constructs the renderer for the product environment

Renderer (one frame, one RenderViewFamily)
  -> StandaloneRenderer or MobileRenderer
  -> owns the frame's single FrameGraph

FrameGraph (one frame)
  -> generic scene passes
  -> VSM passes
  -> view-specific extension passes
  -> product output passes
```

`FrameRenderPipeline` distinguishes Editor and Player behavior. `Renderer` distinguishes platform or performance
topology; the existing `StandaloneRenderer` name remains unchanged and `MobileRenderer` remains the mobile topology.
Renderer, FrameRenderPipeline, FrameGraph, and RenderPass instances are frame-local.

Every structure intended to accelerate later frames is owned by `RenderSystem` or a long-lived service owned by it.
This includes VSM scene caches and physical pages, pipeline caches, per-frame-context transient pools, uniform pools, and
statistics readback slots. A renderer may borrow these services while building or executing its graph but must not own a
cross-frame cache.

## Frame Pipeline And Renderer Contract

`BaseRendererInitParam` owns a `RenderViewFamily`, not a separate scene and single view:

```cpp
struct BaseRendererInitParam
{
    const FrameRenderPipelineData* frameData = nullptr;
    RenderViewFamily viewFamily;
    std::vector<std::unique_ptr<RenderPass>> outputPasses;
};
```

`FrameRenderPipeline::RenderFrame` becomes `void` and stays deliberately small:

```text
begin command list
assemble the frame-local renderer input
construct StandaloneRenderer or MobileRenderer
call renderer.Render()
end command list
```

The pipeline does not prepare VSM work, loop over views to create renderers, or create auxiliary frame graphs.

The renderer performs the frame-local orchestration:

```text
prepare RendererData for the whole family
create its FrameGraph
import family and view resources
register generic DepthPrePass nodes
ask VirtualShadowManager to prepare family data and register VSM nodes
register renderer-specific scene nodes
register view-specific extension nodes
register product output nodes
export final observable resources
compile and execute the graph
```

The Editor must supply one family whose views refer to the same `RTScene`; a mismatched scene is a fatal programming
error. Player supplies a one-view family. Supporting multiple scene families in one product frame is outside this
migration.

## Renderer Data Model

The public/internal type name `RendererData` is retained. Its meaning changes from one view to one family:

```cpp
struct RendererViewData
{
    RenderView view;
    VirtualShadowSamplingSnapshot virtualShadowSampling;
    std::vector<std::shared_ptr<RTRenderItem>> transparentItems;
};

struct RendererData
{
    std::shared_ptr<RTScene> scene;
    std::vector<std::shared_ptr<RTRenderItem>> opaqueItems;
    std::vector<RendererViewData> views;
};
```

The scene's opaque queue is built once because the current renderer has no per-view visibility culling. Transparent
items remain per-view because they are sorted relative to each camera. Future visibility systems may add per-view
visible-item lists without changing the family ownership boundary.

`RendererFrameGraphData` also becomes family-oriented:

```cpp
struct RendererViewFrameGraphData
{
    FrameGraphTextureHandle color;
    FrameGraphTextureHandle depth;
    FrameGraphBufferHandle virtualShadowPageTable;
    VirtualShadowSamplingSnapshot virtualShadowSampling;
};

struct RendererFrameGraphData
{
    std::vector<RendererViewFrameGraphData> views;
    FrameGraphTextureHandle virtualShadowAtlas;
    FrameGraphBufferHandle virtualShadowPhysicalPages;
    FrameGraphBufferHandle virtualShadowStatistics;
    FrameGraphTextureHandle swapchainColor;
};
```

Pass data records an explicit `viewIndex`. Execute callbacks use that index to select `RendererViewData`; FrameGraph no
longer exposes one implicit active view.

## Generic Depth Pre-Pass

`DepthPrePass[view]` is a generic renderer pass, not a VSM pass. It renders the opaque queue into the view's depth
attachment without producing color.

The output depth version has two consumers:

- `VirtualShadowMarkRequestsPass[view]` samples it to identify receiver pages.
- `OpaqueScenePass[view]` loads and reuses it instead of clearing depth.

`StandaloneRenderer` enables the depth pre-pass for each renderable view. `MobileRenderer` may select a different
topology, but any topology that enables VSM must provide the receiver depth dependency.

Keeping this pass generic leaves the depth result reusable by future Hi-Z construction, occlusion culling, and other
non-VSM systems.

## VirtualShadowManager Responsibility

`VirtualShadowManager` remains owned by `RenderSystem`. It retains:

- one `VirtualShadowSceneCache` per live `RTScene`;
- the scene-shared physical atlas, physical metadata, page-table storage, allocator state, and statistics buffer;
- stable view IDs and view-to-page-table-slice registration;
- invalidation tracking and immediately consumed cache-reset state;
- one statistics readback buffer per in-flight frame slot;
- completed-frame statistics consumption.

It no longer records all VSM stages through `PreRenderShadowStep`. During renderer graph setup it:

1. validates and prepares CPU family/view packets;
2. imports the persistent scene resources and current readback slot into the graph;
3. publishes immutable per-view sampling data;
4. registers the focused VSM graph passes.

Preparation mutates the scene cache directly. There is no prepared ticket, commit, abort, rollback, retry, or separate
legacy scheduling path. A later failure terminates the process, so partially prepared state is never consumed by a
subsequent frame.

VSM pass instances and captured pass data are frame-local. Shader and pipeline reuse continues through the long-lived
`ShaderManager`; VSM pass objects do not introduce their own cross-frame pipeline cache.

## VSM Frame-Graph Passes

Focused VSM pass implementations should live under the VirtualShadow render module and carry comments that state their
purpose, scope (family, view, or view/level), and resource contract.

### Family and request preparation

1. `VirtualShadowClearScenePass`
   - Scope: once per family.
   - Reads/writes physical-page metadata.
   - Clears the statistics buffer.
   - Clears resident metadata only when a scene reset is pending.

2. `VirtualShadowClearViewRequestsPass[view]`
   - Scope: once per active view.
   - Writes that view's page-mark slice, page-table slice, and request-count slice.
   - Reads/writes physical metadata to clear the view's per-frame request flags.

3. `VirtualShadowMarkRequestsPass[view]`
   - Scope: once per active view.
   - Reads the generic `DepthPrePass` output.
   - Reads/writes that view's page-mark slice.

4. `VirtualShadowCompactRequestsPass[view]`
   - Scope: once per active view.
   - Reads the page-mark slice.
   - Writes the request-list and request-count slices.
   - Reads/writes scene statistics to accumulate requested pages.

### Residency and deterministic allocation

5. `VirtualShadowResolveHitsPass[view, level]`
   - Scope: every active view and clipmap level, coarse to fine.
   - Reads request-list and request-count slices.
   - Reads/writes page-table, physical metadata, and cached-page statistics.

6. `VirtualShadowAllocatePagesPass[view, level]`
   - Scope: every active view and clipmap level, preserving family/view/coarse-to-fine allocation priority.
   - Reads the page-mark slice.
   - Reads/writes page-table, physical metadata, and allocation statistics.
   - Each node consumes and produces the next logical physical-metadata version. This version chain, rather than a
     manually executed loop, defines deterministic allocation order and prevents later views from reclaiming pinned
     pages.

### Physical page rendering

7. `VirtualShadowClearPhysicalPagesPass[view]`
   - Scope: once per active view.
   - Reads physical metadata.
   - Writes the shared atlas depth attachment, clearing only physical pages selected for redraw.

8. `VirtualShadowRenderCastersPass[view]`
   - Scope: once per active view.
   - Reads physical metadata and the view's caster draws.
   - Loads and writes the shared atlas depth attachment.

9. `VirtualShadowMarkRenderedPass[view]`
   - Scope: once per active view.
   - Reads/writes physical metadata and redraw statistics after the atlas raster work for that view.

### Finalization and diagnostics

10. `VirtualShadowFinalizeScenePass`
    - Scope: once per family.
    - Reads/writes physical metadata and statistics.
    - Finalizes requested/rendered flags and calculates allocated physical pages.

11. `VirtualShadowStatisticsReadbackPass`
    - Scope: once per family.
    - Copies the finalized statistics buffer into the current in-flight readback slot.
    - Records no CPU read and causes no GPU wait.

The implementation may group closely related pass class declarations into a small number of files, but each
FrameGraph node remains independently declared and documented.

## Dependency Model

The intended dependency graph is:

```text
DepthPrePass[view]
  -> MarkRequests[view]
  -> CompactRequests[view]
  -> ResolveHits[view, level]
  -> AllocatePages[view, level]
  -> ClearPhysicalPages[view]
  -> RenderCasters[view]
  -> MarkRendered[view]

ClearScene
  -> ClearViewRequests / ResolveHits / AllocatePages

all MarkRendered
  -> FinalizeScene
  -> StatisticsReadback

DepthPrePass + finalized VSM atlas/page table
  -> Opaque[view]
  -> Transparent[view]
```

`VirtualShadowClearScenePass` and independent `DepthPrePass` nodes have no dependency and may be scheduled in either
order. Their dependent chains join through the resources used by request generation and allocation.

Pass registration order is not an execution contract. Every ordering requirement must be represented by logical
resource versions. The registrar establishes deterministic allocation policy by threading successive logical physical
metadata versions through the selected family/view/level order; FrameGraph, rather than a manual execution loop, then
derives the executable dependency order. Tests interleave unrelated registrations and validate that the compiled order
still follows resource dependencies.

FrameGraph texture and buffer access enums gain explicit `CopySource` and `CopyDestination` values. These accesses are
used by statistics readback and Player output copies. Persistent imported resources participate in the same logical
versioning rules as transient resources.

## View And Product Extensions

`StandaloneRendererInitParam` retains the existing renderer name and supports two explicit extension groups:

```cpp
struct RendererViewPassExtension
{
    UInt32 viewIndex = 0;
    std::vector<std::unique_ptr<RenderPass>> passes;
};

struct StandaloneRendererInitParam : BaseRendererInitParam
{
    bool visualizeVirtualShadowPages = false;
    std::vector<RendererViewPassExtension> viewPasses;
};
```

- `viewPasses` attach Grid, Gizmo, or future view-local effects to one explicit view.
- Base `outputPasses` consume one or more view outputs and produce the product's observable output for either renderer
  topology.
- Editor Overlay reads every view color displayed by ImGui and writes swapchain color.
- Player Copy reads the Player view color and writes swapchain color.

The name `outputPasses` describes data flow, not a manually enforced terminal ordering. FrameGraph dependencies place
these passes after their inputs. The final swapchain version is exported so required upstream nodes are retained.

## FrameGraph API Changes

The graph remains owned and executed inside the renderer. Required API changes are:

- `FrameGraphExecuteContext` references family-oriented `RendererData`.
- `RendererFrameGraphData` stores all view resources and family-shared resources.
- raster and compute execute callbacks return `void`;
- setup callbacks and resource declarations remain declarative;
- `CopySource` and `CopyDestination` access declarations are supported for textures and buffers;
- imported persistent resources are versioned and validated like transient resources;
- passes without a rendering consumer, such as statistics readback, explicitly export their destination version or
  declare a side effect so culling cannot remove them.

`FrameGraph::Compile` continues producing detailed diagnostics for invalid declarations, dependency cycles, or
unsupported resource use. `FrameGraph::Execute` may retain `ErrorCode` for its transient-resource and RHI boundary.
Renderer entry points return `void` and treat compile or execute failure as fatal rather than forwarding it through the
frame pipeline.

## Error Handling

The following internal APIs become `void`:

- `FrameRenderPipeline::RenderFrame`;
- `BaseRenderer::Render`;
- RenderPass graph-registration methods;
- FrameGraph raster and compute execute callbacks;
- focused VSM pass recording methods;
- renderer resource-import and queue-building helpers.

Internal invariants and unrecoverable failures log complete context and use the engine's always-on assertion path to
terminate. Examples include an invalid family, a missing required view target, graph compilation failure, missing
pipeline state, uniform allocation failure, or command-list recording failure.

Low-level RHI primitives may retain result values so the immediate caller can attach context. The current render
pipeline does not recover from them: resource creation, command recording, queue submission, fence, swapchain copy, and
Present failures all log and terminate.

VSM is mandatory. An empty scene, a view without a camera, a missing directional light, disabled directional shadows,
or Metal's current lack of VSM support is a fatal contract violation. Every consumer receives only real atlas, sampler,
and page-table bindings; placeholder and disabled-shadow states do not exist.

## Scene Cache And Statistics Lifetime

The VSM cache and statistics follow a one-way lifetime:

```text
FrameGraph setup
  -> prepare VSM packets and update scene cache immediately
  -> register VSM passes

FrameGraph execute
  -> record VSM work and statistics copy

queue submit succeeds
  -> associate exact submission fence/value with statistics slot

matching FrameContext fence completes
  -> consume the completed statistics slot before it can be reused
  -> RenderSystem publishes the scene-aware value snapshot

any failure
  -> fatal diagnostic
  -> process termination
```

Renderer-owned frame data and immutable VSM results are retained by the existing `FrameContext` submission lifetime.
No pass maps a readback buffer or adds `WaitIdle`. No cache or statistics begin/commit/abort transaction exists.

## Verification

Focused tests must cover:

- `BaseRendererInitParam` owns one family and no single `view` field.
- one renderer invocation creates exactly one FrameGraph for all family views;
- `RendererData` exposes family scene data and indexed per-view data;
- opaque queues are built once and transparent queues are sorted per view;
- DepthPrePass output is consumed by both VSM request marking and Opaque;
- VSM passes declare the documented buffer/texture accesses;
- compiled VSM execution order is correct even when registration order is intentionally different;
- allocation order remains family/view/coarse-to-fine through physical-metadata version chaining;
- all views share scene physical resources while retaining independent targets and page-table slices;
- Grid and Gizmo affect only their selected Editor view;
- Editor Overlay reads all displayed view colors before writing swapchain color;
- Player Copy uses texture copy-source/copy-destination access;
- statistics readback is retained by export/side effect and remains fence-delayed;
- no `ErrorCode` propagation remains from RenderPass or Renderer through `FrameRenderPipeline`.
- no placeholder resource, disabled snapshot, prepared transaction, rollback, retry, legacy pipeline, or pre-render
  scheduler symbol remains.

Real D3D11 and D3D12 smoke tests must continue covering VSM allocation, atlas rendering, buffer copy/readback, and
completed-frame statistics. The VSM frame-graph smoke includes a real indexed caster and executes its draw path for
both views. The full Windows test suite, complete Debug application build, and deterministic Editor startup with
`--project` are required before completion.

## Out Of Scope

- Renaming `StandaloneRenderer`.
- Supporting multiple scene families in one Editor/Player frame.
- Async compute queues or concurrent command-list recording.
- A FrameGraph subgraph abstraction.
- New VSM allocation policies or view-priority rotation.
- New persistent acceleration structures beyond moving their ownership to the established RenderSystem boundary.
