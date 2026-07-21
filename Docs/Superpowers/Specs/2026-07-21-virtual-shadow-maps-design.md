# Virtual Shadow Maps Design

## Purpose

Add a simple but complete first-stage Virtual Shadow Maps (VSM) implementation for VEngine. Here, VSM means a large logical directional-light shadow address space backed by a fixed-size physical depth page pool. Only pages needed by the current view are resident and rendered. Resident pages survive across frames until invalidated or evicted.

The first implementation deliberately uses CPU page requesting, allocation, invalidation, and draw-list construction. It does not depend on compute shaders, unordered-access resources, indirect draws, hardware sparse resources, D3D11.2 tiled resources, or Metal sparse heaps.

The primary product requirement is one shadow-casting directional light. Scene View, Game View, and Player View each own an isolated VSM cache and physical atlas. They share implementation code, shaders, pipelines, and scene proxy data, but never share page mappings, cache history, LRU state, or physical shadow pages.

## Existing Foundation And Missing Capabilities

VEngine already has several required foundations:

- A render-thread-owned `RTScene` containing render items, cameras, and lights.
- Render item local bounds and transforms.
- A light `castShadows` flag propagated to `RTLight`.
- Opaque and transparent renderer queues.
- Per-view renderer invocations and FrameGraph raster passes.
- D3D11, D3D12, and Metal backends with depth attachments, sampled textures, samplers, and raster depth bias descriptors.
- A comparison-sampler description in the common RHI.
- Fence-protected frame contexts and render-resource retirement.

The current renderer cannot implement VSM correctly without the following additions:

- The RHI texture model only exposes `Texture2D`, and sampled depth resources do not separate typeless storage, DSV format, and SRV format on D3D11/D3D12.
- D3D12 does not transition a written depth attachment to shader-read state at render-pass end.
- D3D12 resource binding is hard-coded to four constant buffers, one fragment texture at `t0`, and one fragment sampler at `s0`.
- FrameGraph requires every raster pass to have one color attachment, so it cannot express a depth-only shadow pass.
- Graphics pipelines require both vertex and fragment shaders and always configure one color target.
- Renderer instances are temporary. `EditorRenderFramePipeline` constructs a new `StandaloneRenderer` for each view every frame, so a renderer cannot own cross-frame shadow state.
- Scene View camera snapshots may be recreated each frame, so camera pointer identity cannot represent persistent view identity.
- Math does not yet provide AABB, plane, frustum, or transformed-bounds helpers.
- Render items do not expose stable IDs, shadow flags, or revisions suitable for cache invalidation.
- The current forward shader does not provide world position to the fragment stage and does not sample shadow data.

## Goals

The first implementation must provide:

- One main directional light with dynamic virtual shadows.
- Four directional-light clipmap levels per view.
- A nominal 16K-by-16K logical shadow-map resolution per clipmap level.
- 128-by-128 physical pages with a one-texel PCF gutter inside each page.
- A fixed-size physical D32 atlas per view.
- CPU receiver-page requests derived from visible render-item bounds.
- CPU page allocation, pinning, LRU eviction, invalidation, and caster culling.
- Cross-frame reuse of clean resident pages.
- Correct invalidation for caster add, remove, transform, mesh, bounds, and shadow-flag changes.
- A depth-only page rendering pass.
- A compact GPU resident-page lookup table stored in a constant buffer.
- Coarser-clipmap fallback for missing fine pages.
- Three-by-three percentage-closer filtering.
- Independent Scene View, Game View, and Player caches.
- Functional parity on Windows D3D11 and D3D12.
- A common RHI shape that maps naturally to Metal, with Metal implementation and macOS verification included in the feature rollout.
- Diagnostics for requested, resident, cached, dirty, rendered, evicted, and missing pages.

## Non-Goals

The first implementation does not add:

- Point-light, spot-light, or multiple-directional-light virtual shadows.
- GPU scene-depth feedback or GPU page marking.
- Compute shaders, UAVs, atomics, scans, indirect dispatch, or indirect draw.
- Hardware sparse resources, D3D11.2 tiled resources, or Metal sparse heaps.
- Alpha-tested, world-position-offset, tessellated, or skinned shadow casters.
- Separate static and dynamic depth caches.
- Cross-view page sharing or a global physical shadow pool.
- Shadow Map Ray Tracing or contact-hardening soft shadows.
- Async compute, bindless resources, or a general descriptor-management redesign.
- A deferred renderer or screen-space shadow-mask pass.

## First-Stage Configuration

The default configuration is explicit and bounded:

```text
Clipmap level count:           4
Nominal virtual resolution:    16384 x 16384 texels per level
Physical page size:           128 x 128 texels
Virtual pages per level axis: 128
PCF gutter:                   1 texel inside each physical page edge
Usable page interior:         126 x 126 filtered depth samples
GPU page hash capacity:       2048 entries
Maximum resident Game pages:  1024
Maximum resident Scene pages: 256 by default
Game/Player atlas:            4096 x 4096 D32, approximately 64 MiB
Scene View atlas:             2048 x 2048 D32, approximately 16 MiB
Default shadow distance:      200 engine units
PCF kernel:                   3 x 3
```

All values are represented by a `VirtualShadowMapConfig`. Resource-shape changes such as page size, atlas extent, clipmap count, or virtual resolution rebuild that view's VSM resources. Sampling-only changes such as depth bias, normal bias, and PCF tuning do not invalidate depth pages.

The 16K resolution is the logical address-space convention: 128 virtual pages times a nominal 128 texels per page. The one-texel gutter is stored inside each 128-by-128 physical slot, leaving a 126-by-126 filtered interior. This reduces effective filterable texel density by 1.5625 percent, but preserves a power-of-two atlas and avoids extra page-table lookups for PCF taps that cross a page edge. Page addressing and clipmap coverage continue to use the 128-by-128 virtual page grid; the shader maps each logical page domain into its 126-by-126 physical interior.

## Per-View Isolation And Lifetime

VSM state is attached to a persistent render view, not to `BaseRenderer`, `StandaloneRenderer`, or an `RTCamera` pointer.

```text
SceneViewPanel
  -> RenderViewState
      -> RTRenderViewState
          -> VirtualShadowViewCache

GameViewPanel
  -> RenderViewState
      -> RTRenderViewState
          -> VirtualShadowViewCache

Player
  -> RenderViewState
      -> RTRenderViewState
          -> VirtualShadowViewCache
```

`RenderViewState` is the scene-thread-visible lifetime object. It owns an `RTRenderViewState` proxy whose mutable state and live RHI objects are used only on the Render Thread. `StandaloneRendererInitParam` and the player renderer input carry the persistent RT view state alongside the current scene, camera snapshot, and target.

Each `VirtualShadowViewCache` owns:

- Its physical depth atlas.
- Its physical-page allocator.
- Its virtual-to-physical page map.
- Its free list and LRU records.
- Its clipmap history and depth epoch.
- Its per-caster last-seen revisions and previous world bounds.
- Its current GPU resident-page hash table.
- Its per-frame page and draw statistics.

Moving the Scene View camera cannot allocate, evict, dirty, or rewrite a Game View page. Closing one view retires only that view's atlas and cache resources. Resizing a color RenderTexture does not rebuild the VSM atlas because the shadow budget is view policy rather than color-target extent.

Immutable VSM shaders, pipeline variants, and sampler descriptions remain shared through the normal render and shader systems. Isolation applies to mutable page state and depth contents, not to implementation code.

## Module Boundaries

The design uses small units with explicit ownership:

### `RenderViewState` And `RTRenderViewState`

- Establish stable cross-frame view identity.
- Own the per-view VSM cache lifetime.
- Provide the current shadow configuration and view role.
- Retire render resources through existing frame-context lifetime rules.

### `VirtualShadowMapConfig`

- Stores virtual resolution, page size, atlas extent, clipmap count, shadow distance, hash capacity, and sampling settings.
- Validates power-of-two and size relationships before resource creation.
- Produces a resource-shape revision when allocation-relevant fields change.

### `VirtualShadowPageCache`

- Owns CPU-only virtual-page metadata.
- Allocates from the free list and evicts from the LRU.
- Pins requested pages for the current view frame.
- Marks new or invalidated pages dirty.
- Produces the final resident-page hash table.
- Has no RHI dependency.

### `VirtualShadowClipmap`

- Builds the directional-light basis.
- Quantizes XY clipmap origins and the light-space depth origin.
- Builds page request ranges from camera-visible receiver bounds.
- Calculates the clipmap level used by a receiver distance range.
- Generates stable virtual page keys.

### `VirtualShadowCasterCuller`

- Converts render-item world bounds into light-space AABBs.
- Invalidates pages affected by old and new caster bounds.
- Builds the caster list for each dirty page.

### `VirtualShadowMapRenderPass`

- Imports the persistent view atlas into FrameGraph.
- Registers the depth-only page update pass when dirty pages exist.
- Clears and draws each dirty physical page.
- Publishes the final atlas handle for later scene passes.

### Forward Scene Passes

- Declare a shader read of the per-view shadow atlas.
- Bind the current view's virtual-shadow constant buffer, atlas, and comparison sampler.
- Apply virtual shadowing to direct directional-light illumination.

## RHI Changes

### Sampled Depth Textures

A texture with both `DepthStencil` and `Sampled` usage has one logical `RhiFormat::Depth32Float`, but backend storage and view formats differ:

```text
D3D11/D3D12 resource storage: R32_TYPELESS
D3D11/D3D12 DSV:              D32_FLOAT
D3D11/D3D12 SRV:              R32_FLOAT
Metal texture:                MTLPixelFormatDepth32Float
```

D3D11 unbinds the DSV before binding the same resource as an SRV. D3D12 transitions the physical atlas between depth-write and pixel-shader-resource states. Metal ends the render encoder before sampling the atlas in the following pass.

### Depth-Only Render Passes

`RhiRenderPassBeginInfo` explicitly records whether a color attachment exists. A null color texture no longer implicitly means the swapchain when the pass is declared depth-only. Backends bind zero color targets and one depth attachment for the VSM pass.

FrameGraph accepts a raster pass with a depth attachment and no color attachment. Validation requires at least one attachment, validates depth extent independently, and derives the render area from depth when color is absent.

### Vertex-Only Pipelines

`RhiBoundShaderStateDesc::fragmentShader` becomes optional. `RhiGraphicsPipelineDesc` records zero or one color attachments in the first implementation. D3D11 binds a null pixel shader, D3D12 uses `NumRenderTargets = 0`, and Metal uses a nil fragment function and invalid color pixel format.

This supports both the shadow caster pipeline and a draw-based local depth clear pipeline.

### Small Explicit Resource Layout

The D3D12 fixed root signature is replaced by a small pipeline resource layout. Each layout entry declares:

```text
Resource kind: UniformBuffer / SampledTexture / Sampler
Shader stage:  Vertex / Fragment
Register slot
```

D3D12 builds root parameters from the declared layout and stores the mapping from `(kind, stage, slot)` to root parameter. One-descriptor tables are sufficient for the first implementation. D3D11 and Metal bind their native stage slots and use the layout for validation.

The first VSM-capable forward layout is:

```text
b0 FrameConstants
b1 ViewConstants
b2 ObjectConstants
b3 MaterialConstants
b4 VirtualShadowConstants and resident-page hash table

t0 MainTexture
t1 PhysicalShadowDepthAtlas

s0 MaterialSampler
s1 ShadowComparisonSampler
```

This is a bounded improvement required by VSM, not a general bindless or descriptor-set redesign.

## FrameGraph Flow

Each view retains its current self-contained renderer graph:

```text
Optional VirtualShadowDepthPass
  -> OpaqueScenePass
  -> TransparentScenePass
  -> Optional editor grid/gizmo passes
```

The view cache imports its persistent physical atlas. When dirty pages exist, `VirtualShadowDepthPass` writes a new logical depth version with `Load` and `Store`. The opaque and transparent passes declare shader reads of that version. When no pages are dirty, no shadow update pass is registered and scene passes read the imported cached atlas directly.

The atlas must use `Store`; clearing or discarding the complete attachment would destroy cached pages. FrameGraph inference preserves the atlas because scene passes consume it after the shadow update.

## Clipmap Coordinates And Page Keys

The directional light defines an orthonormal basis:

```text
LightRight
LightUp
LightDirection
```

For each clipmap level, the camera position transformed into light space is quantized to that level's page-world size:

```text
pageWorldSize = clipmapWorldExtent / 128
originX = floor(cameraLightX / pageWorldSize) * pageWorldSize
originY = floor(cameraLightY / pageWorldSize) * pageWorldSize
```

The four default clipmap radii are derived from the shadow distance as one eighth, one quarter, one half, and the full shadow distance. Every level retains a 16K logical resolution while world coverage doubles.

Light-space depth uses a fixed span of twice the shadow distance. Its center is quantized to a stable depth step. The resulting signed depth-cell coordinate is part of the page key. Moving within the current depth cell preserves cached depth projection; crossing into another cell changes the depth epoch and requests new keys. Changing light orientation invalidates the complete view cache.

The GPU key is two 32-bit words so D3D11 Shader Model 5 does not require 64-bit integer operations:

```text
key0 bits  0..15: signed 16-bit absolute page X, two's complement
key0 bits 16..31: signed 16-bit absolute page Y, two's complement
key1 bits  0..7:  unsigned clipmap level
key1 bits  8..31: signed 24-bit depth epoch, two's complement
```

The empty-key sentinel is `key0 = 0xFFFFFFFF` and `key1 = 0xFFFFFFFF`. It cannot conflict with a valid first-stage key because valid clipmap levels are zero through three. The signed page and depth-epoch ranges are sufficient for the first-stage shadow distance and ordinary engine world coordinates. Configuration validation rejects a setup whose quantized coordinates cannot be represented.

## CPU Page Request Generation

The first implementation does not read GPU scene depth. It conservatively requests pages from visible receiver bounds:

1. Build the current camera frustum, including perspective and orthographic modes.
2. Transform each render item's local AABB into a world AABB.
3. Frustum-cull the world AABB.
4. Skip items whose `receiveShadows` flag is false.
5. Determine which clipmap distance intervals overlap the receiver's camera-depth range.
6. Transform the receiver bounds into light space.
7. Intersect its XY projection with the corresponding clipmap square and the light-space bounds of that camera-frustum slice.
8. Convert the result to absolute virtual page coordinates.
9. Deduplicate page keys and retain their priority.

Priority is ordered by nearer clipmap level, larger estimated screen coverage, and shorter camera distance. Bounds-based requests can over-request around large objects, but they do not omit a visible receiver contained by a correct bound.

If the physical pool cannot hold every request, low-priority far pages remain nonresident. The shader searches coarser clipmap levels before treating the sample as fully lit.

## Physical Page Allocation And LRU

Each physical page has one residency state:

```text
Free
ResidentClean
ResidentDirty
```

`PinnedThisFrame` is an independent flag on a resident page rather than a fourth residency state. A clean or dirty page can be pinned, while a free page cannot.

At view-frame begin, the cache removes previous-frame pins. After the full page request set is known:

1. Existing requested pages update `lastUsedFrame` and become pinned.
2. New requests allocate from the free list.
3. If the free list is empty, allocation evicts the oldest unpinned resident page.
4. New allocations become dirty and are not exposed as valid shader entries until rendered.
5. Requests that cannot allocate remain missing and participate in coarse-level fallback.
6. After dirty pages render successfully, their entries become valid and clean.

An allocation must never overwrite a physical page that is pinned by the current view frame. Page generation counters are retained in development builds to diagnose stale mappings.

## GPU Resident-Page Hash Table

Only resident valid mappings are uploaded. The first implementation uses a 2048-entry open-addressed hash table at no more than 50 percent load.

Each 16-byte constant-buffer entry contains:

```text
uint key0
uint key1
uint physicalPageIndex
uint flags
```

`flags` bit zero is `Valid`; all other first-stage bits are zero. Dirty and newly allocated pages are omitted from the hash table until their page draws have been recorded, so shaders never treat uninitialized physical depth as resident.

The page table occupies 32 KiB. Clipmap origins, basis vectors, dimensions, depth projection, atlas scale, bias settings, and flags fit in the remainder of the D3D11 64 KiB constant-buffer limit.

CPU insertion and shader lookup use the same integer hash and a maximum of 16 linear probes. A mapping that cannot be inserted within that limit is reported as missing rather than allowing CPU/GPU disagreement. An empty-slot sentinel terminates lookup early.

## Scene Data And Cache Invalidation

`LightComponent` and `RTLight` expose:

```text
castShadows
shadowDistance
depthBias
normalBias
shadow configuration revision
```

`MeshRenderComponent` and `RTRenderItem` expose:

```text
stable render item ID
castShadows
receiveShadows
monotonically increasing revision
```

Transform, mesh, bounds, and shadow-flag changes increment the render-item revision. Pure color-material changes do not invalidate depth in the opaque-only first implementation. Later alpha-test or vertex-deformation support must add the relevant material and deformation revisions.

Each view cache stores the last seen revision and previous world bounds for every caster ID. During update:

- A newly visible caster invalidates pages overlapping its new bounds.
- A removed caster invalidates pages overlapping its saved old bounds.
- A changed caster invalidates pages overlapping both old and new bounds.
- A light orientation change invalidates all pages in that view.
- Shadow resource-shape changes rebuild the cache and atlas.
- Bias and filtering changes alter sampling only and do not redraw pages.
- Camera motion changes requests and LRU usage but does not clear the cache.
- A camera cut clears request history but retains any pages found through stable absolute keys.

Scene View and Game View independently observe the same scene revisions. Consuming a revision in one view cannot suppress invalidation in another view.

## Dirty-Page Caster Culling

For every dirty page, the cache builds a light-space page volume:

- XY equals the page's world extent expanded for the one-texel gutter.
- Z equals the current quantized shadow depth range.

Opaque render items with `castShadows` enabled are transformed to light-space AABBs. Items intersecting the page volume enter that page's caster list. One item may appear in multiple dirty-page lists. Transparent objects do not cast shadows in the first implementation.

The first implementation records direct draws per page and caster. It does not batch page draws through indirect arguments. The diagnostic counters expose page and draw counts so a later GPU-culling milestone can target measured bottlenecks.

## Page Rendering

The shadow atlas is never cleared as a complete attachment during normal operation. `VirtualShadowDepthPass` loads existing contents and updates dirty pages only.

For every dirty page:

1. Set viewport and scissor to the physical page rectangle.
2. Bind a depth-only clear pipeline.
3. Draw a fullscreen triangle at depth 1.0 to clear only that page.
4. Bind the shadow caster pipeline.
5. Upload the page's orthographic light view-projection matrix.
6. Draw the page's caster list using each object's normal vertex and index buffers.
7. Mark the page valid and clean only after command recording succeeds.

The clear pipeline uses no color target, no fragment shader, depth compare `Always`, and depth writes enabled. The caster pipeline uses no color target, no fragment shader, depth writes enabled, front-face culling, configurable constant and slope-scaled depth bias, and the engine's 0-to-1 depth convention.

The physical 128-by-128 page includes a one-texel gutter. The page projection expands to render correct depth into the gutter so comparison samples cannot leak into an unrelated adjacent physical page.

## Forward Shader Sampling

The mesh vertex shader provides world position and world normal to the fragment shader. For a receiving fragment, the fragment shader:

1. Selects a clipmap level from camera-relative distance.
2. Projects the world position into that clipmap's light-space coordinates.
3. Builds the absolute virtual page key.
4. Looks up the physical page through the resident-page hash table.
5. Repeats with coarser levels when the selected level is missing.
6. Computes page-local coordinates and physical atlas UV.
7. Applies receiver normal bias and depth bias.
8. Executes a three-by-three comparison-sampler PCF kernel inside the gutter-safe region.
9. Multiplies only direct directional-light illumination by the shadow visibility.

Ambient illumination remains unshadowed. `receiveShadows == false` bypasses page lookup. Opaque and transparent scene passes can receive shadows because they use the same view shadow inputs; only opaque objects cast in the first implementation.

Hard clipmap selection is acceptable for the first implementation. Coarser-level fallback prevents missing fine pages from creating invalid texture reads. A future quality milestone may add dithered or blended transitions.

## Failure Handling And Degradation

- If a view's atlas, sampler, shader, or pipeline creation fails, VSM is disabled for that view and scene rendering continues fully lit.
- Missing direction lights and lights with `castShadows` disabled omit the shadow pass and upload a disabled shadow flag.
- Pool pressure drops low-priority far pages and increments missing-page statistics.
- Missing hash entries fall back to coarser clipmaps and then fully lit.
- Invalid or non-finite bounds skip the affected caster or receiver and produce a rate-limited diagnostic.
- A dirty page is not exposed as valid until its clear and caster draws are recorded successfully.
- Page-table insertion failure is treated as a missing page and cannot leave a stale physical mapping visible.
- View destruction retires the atlas and dependent RHI objects through a fence-safe frame context.
- Device loss or RHI shutdown destroys all RT view states before destroying the device.

## Diagnostics And Editor Visualization

Every view exposes these counters:

```text
Requested
Resident
Allocated
Cached
Dirty
Rendered
Evicted
Missing
CasterDraws
```

Warnings for pool or hash-table pressure are rate limited and name the view. The Editor provides initial debug views for:

- Physical atlas depth.
- Virtual page address and selected clipmap level.
- Resident, dirty, and missing page coloring.
- Caster bounds and dirty-page overlap.

Scene View and Game View statistics are displayed separately, which also verifies cache isolation.

## Backend Policy

D3D11 and D3D12 are required Windows acceptance backends. The upper VSM algorithm is identical on both. D3D11 acts as a compatibility backend and may have higher CPU and draw overhead, but it is not a reduced-feature shadow path.

Metal implements the same sampled-depth, depth-only-pass, vertex-only-pipeline, resource-layout, and comparison-sampling contracts. The feature is not considered cross-platform complete until it compiles and runs on the existing macOS player/editor path. Windows implementation and verification may precede the macOS verification checkpoint, but common headers and renderer code must not contain D3D-specific assumptions.

D3D11.2 tiled resources remain an optional future optimization. The first implementation's fixed committed atlas and logical page mappings work on ordinary D3D11 feature-level 11 hardware without tiled-resource support.

## Verification Strategy

The user-approved design explicitly requires focused tests for new CPU page-management and math behavior.

### CPU Unit Coverage

- Virtual page key packing, unpacking, equality, and hash stability.
- Resident-page hash insertion, collision handling, lookup, missing lookup, and probe-limit failure.
- Free-list allocation and LRU eviction.
- Protection of current-frame pinned pages.
- Priority degradation when requests exceed physical capacity.
- Clipmap XY quantization and depth-epoch changes.
- Perspective and orthographic frustum construction.
- Local-to-world AABB transformation.
- Receiver page request generation.
- Old and new caster-bounds invalidation.
- Caster add and remove invalidation.
- Full invalidation after light orientation changes.
- Complete isolation between two pure CPU `VirtualShadowPageCache` instances.

### RHI Integration Coverage

RHI, FrameGraph, renderer, window, and device-lifetime behavior is not covered by additional unit-test or CTest executables. It is verified through the existing Editor and Player integration paths with backend debug layers enabled:

- Create and use a `DepthStencil | Sampled` atlas through a real Editor or Player view.
- Record a depth-only pass without a fragment shader, then sample the result from a following scene pass.
- Bind multiple sampled textures and samplers through the pipeline resource layout.
- Validate comparison-sampler output through the shadow acceptance scene.
- Exercise D3D12 `DepthWrite -> ShaderRead -> DepthWrite` transitions without debug-layer errors.
- Exercise D3D11 DSV/SRV hazard unbinding without debug-layer errors.

### Scene Acceptance

- A static DemoProject scene reaches a state where clean cached pages dominate and dirty-page renders approach zero.
- Slow camera movement renders only pages entering the working set.
- A moved caster removes its old shadow and creates its new shadow.
- Scene View camera motion does not change Game View resident, LRU, atlas, or statistics state.
- Scene View and Game View render correctly with different clipmap origins and atlas budgets in the same frame.
- Camera cuts do not expose stale or unrelated physical pages.
- Pool overflow produces missing distant shadows without crashes, out-of-range access, or page corruption.
- PCF does not sample unrelated adjacent physical pages.
- View destruction, RenderTexture replacement, and RHI shutdown produce no lifetime or debug-layer errors.
- D3D11 and D3D12 show equivalent page residency and visually equivalent shadow coverage for the same view.
- Metal passes the equivalent macOS player/editor smoke scene.

## Implementation Order

The feature should be implemented through independently verifiable stages:

1. Sampled depth textures, depth-only passes, vertex-only pipelines, and explicit resource layouts across the RHI backends.
2. AABB, plane, frustum, transformed bounds, shadow flags, stable render-item IDs, and revisions.
3. Persistent `RenderViewState` and `RTRenderViewState` ownership for Scene View, Game View, and Player.
4. CPU virtual page keys, page cache, hash table, pinning, and LRU.
5. Clipmap quantization, receiver requests, invalidation, and dirty-page caster culling.
6. Per-view physical atlas creation and `VirtualShadowDepthPass` page updates.
7. Forward shader lookup, coarse fallback, bias, and three-by-three PCF.
8. Editor statistics, visualizations, pool-pressure diagnostics, and default tuning.
9. Complete Windows D3D11/D3D12 validation followed by Metal compile and runtime validation.

Each stage must leave the existing unshadowed renderer functional. Before shader integration is enabled, VSM resources and passes are exercised through the existing Editor and Player integration paths and debug visualization without changing the default scene output.

## Completion Criteria

The first implementation is complete when all of the following are true:

- DemoProject renders stable dynamic directional-light shadows on D3D11 and D3D12.
- Scene View and Game View render shadows in the same editor frame with fully isolated caches.
- A static scene reuses clean pages instead of redrawing the complete working set.
- Camera movement allocates only pages entering the current view's requested working set.
- Caster movement invalidates both old and new affected page regions.
- Missing pages degrade to a coarser clipmap or fully lit result without invalid memory access.
- D3D debug layers report no DSV/SRV hazards, invalid resource states, or use-after-free behavior.
- View closure, target recreation, and shutdown release physical atlases safely.
- Metal implements and validates the common VSM RHI contracts on the macOS path.
- Architecture and configuration limits are reflected in the canonical render documentation.

## References

- Unreal Engine Virtual Shadow Maps: <https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine>
- Unreal Engine 5.1 per-view VSM cache notes: <https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5.1-release-notes?application_version=5.1>
- Unreal Engine `FSceneViewStateInterface`: <https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FSceneViewStateInterface>
- D3D11.2 tiled resources, reserved as a future optional optimization: <https://learn.microsoft.com/en-us/windows/win32/direct3d11/direct3d-11-2-features>
