# VSM View Family And Shared Physical Page Pool Design

## Purpose

Restructure VEngine's directional-light Virtual Shadow Map path so all views of one `RTScene` in one frame are prepared together before any view performs its formal scene render. The change introduces a lightweight `RenderViewFamily`, moves page request and physical-page work into a `VirtualShadowManager` owned by `RenderSystemImpl`, and replaces per-view physical page pools with one pool per `RTScene`.

The formal renderer remains single-view. This design does not turn the view family into a FrameGraph and does not merge the scene renderers for different views.

This specification supersedes the per-view physical atlas, per-view physical metadata, and "cross-view physical page sharing" non-goal in `2026-07-23-gpu-driven-vsm-design.md`. The existing GPU-driven marking, allocation, page rendering, and forward-sampling behavior otherwise remains the implementation baseline.

## Scope

The change supports:

- A lightweight family containing all views that render one `RTScene` during one frame.
- Editor Scene View and Game View in the same family.
- A one-view family for the Player path.
- One `VirtualShadowManager` owned by `RenderSystemImpl`.
- One persistent `VirtualShadowSceneCache` per live `RTScene`.
- One physical page atlas and allocator shared by all views of that scene.
- Per-view page-table slices in scene-owned shared GPU storage.
- Stable automatically assigned view identifiers.
- Absolute light-space logical page coordinates.
- One family-wide `PreRenderShadowStep` before any formal view renderer runs.

The change does not add:

- A family-level FrameGraph.
- Multi-view scene drawing inside `BaseRenderer`.
- Cross-scene physical page sharing.
- Cross-view logical-page deduplication.
- Point-light or spot-light VSMs.
- View-priority rotation or round-robin allocation.
- Dynamic resolution or automatic pool-pressure quality reduction.
- A projection epoch in the physical page key.

## Ownership Model

### RenderSystemImpl

`RenderSystemImpl` owns one long-lived `VirtualShadowManager`. It initializes the manager after the RHI device is available and destroys it before the device is released.

The manager's lifetime is the render-system lifetime, but its physical resources are partitioned by scene:

```text
RenderSystemImpl
  -> VirtualShadowManager
       -> VirtualShadowSceneCache(RTScene A)
       -> VirtualShadowSceneCache(RTScene B)
```

No physical atlas, page table, allocator state, or page metadata crosses an `RTScene` boundary.

### VirtualShadowSceneCache

Each live `RTScene` receives one cache containing:

- The physical depth atlas.
- Physical page metadata.
- Physical page allocation state.
- The GPU lookup structure for logical page keys.
- Shared GPU page-table storage.
- Per-view page-table slice allocation state.
- Scene shadow invalidation state.
- Current-frame pin/request state.
- Allocation and overflow diagnostics.

The cache is created lazily when the first shadow-enabled family for a scene is prepared. It is released when the scene is destroyed or explicitly unregistered from the manager.

### RenderViewState

`RenderViewState` retains only persistent state that is private to one view:

- A stable `UInt32 viewID`.
- The page-table slice assigned by the current scene cache.
- Per-level clipmap origins and working ranges.
- Projection compatibility data.
- A monotonically increasing `projectionRevision`.
- Previous-frame statistics needed by the view.

It no longer owns a physical atlas, physical page metadata, or a physical page allocator.

The manager assigns `viewID` when a `RenderViewState` is first registered. The state retains the identifier across frames. Identifiers are not immediately reused; the initial implementation does not reuse them during one `RenderSystemImpl` lifetime.

The packed key reserves 24 bits for `viewID`. If the allocator reaches `0x00ffffff`, new views cannot enable VSM and use the normal no-shadow fallback. Wraparound is forbidden because it could alias resident pages belonging to an older view.

When a view moves to a different `RTScene`, its old page-table slice is released and a new slice is assigned in the destination scene cache. Its stable `viewID` does not change, but the two scene caches remain physically isolated.

## View And Family Data

`RenderView` is a lightweight single-frame descriptor. It carries the camera, render target, persistent `RenderViewState`, fill mode, and any renderer-selection data already required by the current pipeline.

`RenderViewFamily` contains:

```cpp
struct RenderViewFamily
{
    RTScene* scene = nullptr;
    std::vector<RenderView> views;
};
```

Every view in a family must reference `family.scene`. A family with mismatched scenes is rejected before shadow command recording.

The Editor builds one family from the Scene View and Game View when both render the same scene. The Player constructs a family with one view. Views that render different scenes are placed in different families and invoke `PreRenderShadowStep` separately.

The vector order is the authoritative deterministic view order for preparation and later rendering.

## Logical Page Identity

VEngine uses the absolute-coordinate approach rather than Unreal Engine's explicit previous-to-current page-table panning.

The logical identity is:

```text
(viewID, clipmapLevel, absolutePageX, absolutePageY)
```

It is packed into the existing two-word GPU representation:

```text
key0 = uint16(int16(absolutePageX))
     | uint32(uint16(int16(absolutePageY))) << 16

key1 = uint32(uint8(clipmapLevel))
     | (viewID & 0x00ffffff) << 8
```

`absolutePageX` and `absolutePageY` must fit signed 16-bit values. A page outside that representable range is not allocated and is reported as a range failure. The key contains neither `depthEpoch` nor `projectionRevision`.

Absolute coordinates are based on a stable light-space coordinate system:

1. The directional-light direction defines a deterministic light-space basis.
2. The light-space XY origin is derived from the world origin, not the camera.
3. Each level has a fixed `pageWorldSize`.
4. `absolutePageX/Y` are the floor of light-space XY divided by that level's `pageWorldSize`.
5. The clipmap working origin is the camera's absolute page coordinate minus half the page-table dimension.

This makes a logical page's world-space XY footprint stable while the camera moves.

Because `viewID` is part of the key, two views do not share one logical cache entry even when they use the same light, level, and absolute XY coordinates. They share the physical atlas capacity and allocator, but each resident physical page has one view-scoped logical owner. Cross-view content deduplication would require a light/projection identity instead of `viewID` and is outside this change.

## Clipmap Origin Movement

Each clipmap origin is snapped to a virtual-page boundary. Camera movement smaller than one page leaves the origin unchanged. When the camera crosses a page boundary, the view computes a new absolute working range.

The current dense page-table slice is cleared or rewritten for the new working range, then current requests look up their absolute keys in the scene cache:

- A key already resident reuses its existing physical page.
- A key newly exposed at the clipmap edge is allocated and rendered.
- A key no longer inside the current working range is omitted from the view's page table but may remain resident as a cache candidate.

The physical atlas content is never copied merely because the clipmap origin moved. Origin panning does not increment `projectionRevision` and does not dirty overlapping pages.

This produces the same reuse result as Unreal Engine's clipmap page-table panning while fitting VEngine's existing absolute page identity.

## Projection Compatibility

`RenderViewState` keeps a projection compatibility snapshot. It includes the directional-light basis, clipmap level configuration, page-world sizes, shadow-distance/depth-range configuration, and the quantized depth anchor used by the current clipmap projection.

The following changes are compatible and retain resident pages:

- View translation in the stable light-space XY plane.
- Clipmap origin movement by any integer number of pages.
- A camera cut whose resulting projection snapshot is otherwise compatible.
- Viewport visibility changes that only request a different subset of pages.

The following changes increment `projectionRevision` and invalidate all resident pages owned by the affected view:

- A quantized depth-anchor change.
- A change to clipmap level count or level numbering.
- A change to page-world size, shadow distance, or depth range.
- Another change that makes the stored physical-page projection differ from the projection used for sampling.

A directional-light direction change changes the light-space basis and invalidates the directional VSM pages for every view in the scene cache.

The depth anchor should use a deliberately broad, quantized range so ordinary camera movement along the light direction does not cause frequent full invalidation. The anchor participates only in compatibility testing; it is not part of the page key.

## Family Frame Flow

The frame pipeline records work in this order:

```text
Begin command list
  -> prepare/update RTScene render resources
  -> build RenderViewFamily
  -> VirtualShadowManager::PreRenderShadowStep
  -> Renderer(View 0)
  -> Renderer(View 1)
  -> ...
  -> editor overlay or player presentation
End command list
```

`PreRenderShadowStep` records RHI commands directly. It does not create a family FrameGraph.

For one family it performs:

1. Validate the scene and view descriptors.
2. Create or retrieve the scene cache.
3. Register new views and assign their page-table slices.
4. Reset current-frame request and pin state.
5. Update clipmaps and evaluate projection compatibility for every view.
6. Render or prepare receiver depth for every valid shadow-enabled view.
7. Mark and compact logical page requests for every view.
8. Combine the compact request streams in family order.
9. Resolve resident hits in the scene cache.
10. Allocate physical pages for misses.
11. Apply scene/caster invalidations.
12. Clear and render newly allocated or dirty requested pages.
13. Finalize the shared page table and physical metadata.
14. Produce an immutable `VirtualShadowViewResult` for each view.

The combined request stream is a family-wide work list, not cross-view deduplication. Deduplication occurs only for identical full keys, which means repeated requests within one view can merge while requests from different `viewID` values remain distinct.

## Deterministic Allocation And Isolation

Requests are processed in this stable order:

```text
RenderViewFamily vector order
  -> clipmap level order
  -> compacted logical page order
```

The existing coarse-to-fine level priority remains so a view establishes coarse fallback coverage before spending capacity on finer levels. There is no view-order rotation between frames.

Before allocation begins, cache hits requested by all family views are identified and pinned for the frame. Newly allocated pages are also pinned immediately. A later view may not evict or overwrite a physical page requested by an earlier view in the same family.

The allocator uses available pages and may reclaim only pages that are not pinned or requested by the current family. If no eligible physical page remains, the request stays unmapped and increments the overflow count. Parameter sizing is expected to keep this exceptional.

These rules ensure that a later view can change its own page-table slice and allocate from the common pool without corrupting mappings already prepared for an earlier view.

## Renderer Integration

The formal renderer continues to operate on exactly one view.

`BaseRendererInitParam` receives the corresponding immutable `VirtualShadowViewResult`. The result contains or references:

- The shared physical atlas.
- The shared GPU page-table storage.
- The current view's page-table slice.
- Clipmap sampling constants.
- Receiver depth readiness.
- An enabled/disabled status.

`BaseRenderer` no longer calls the current per-view `PrepareVirtualShadows` allocation path. It imports or binds the manager-prepared resources and builds only the view's normal renderer graph.

`StandaloneRenderer` no longer adds `GpuVirtualShadowRenderPass`. The compute shaders, page allocator stages, physical page clear, and page caster rendering currently coordinated by that pass move behind `VirtualShadowManager`.

Opaque rendering loads the receiver depth prepared for the view rather than clearing it. Transparent rendering and editor-injected renderer passes retain their current single-view behavior.

The manager does not own opaque, transparent, grid, gizmo, overlay, or presentation rendering.

## Scene-Shared And View-Private State

State shared through the `RTScene` cache:

- Physical page atlas.
- Physical page metadata and allocator.
- Shared page-table GPU buffer.
- View-slice allocator.
- Directional-light compatibility state.
- Caster snapshots and invalidation journal.
- Family-frame request, pin, and dirty work buffers.
- Pool statistics and diagnostics.

State kept private to a view:

- Stable `viewID`.
- Camera and render target.
- Clipmap origins and working ranges.
- Projection compatibility snapshot and revision.
- Page-table slice assignment.
- Receiver depth.
- Sampling constants and per-view result.

The first implementation may keep `VirtualShadowSceneCache` private to `VirtualShadowManager`; `RTScene` is the ownership key and lifetime boundary, not necessarily the C++ storage location.

## Invalidation

Clipmap origin movement alone does not invalidate cached content.

Scene invalidation behaves as follows:

- A new opaque shadow caster dirties pages overlapped by its current light-space bounds.
- A moved caster dirties pages overlapped by both its previous and current bounds.
- A removed or newly non-shadow-casting caster dirties pages overlapped by its previous bounds.
- A material or geometry change that can alter shadow depth is treated as caster movement for invalidation.
- A directional-light basis change invalidates all directional VSM pages in the scene cache.
- A view-only incompatible projection change invalidates only pages whose key contains that view's `viewID`.

Invalidated resident pages remain dirty until requested and rendered. An unrequested dirty page is not rendered speculatively.

Releasing a view removes its page-table slice. Its physical pages may remain temporarily and are reclaimed lazily because their keys cannot be confused with another non-reused `viewID`.

## Failure And Overflow Handling

Recoverable VSM failures do not abort normal scene rendering:

- An invalid view disables VSM only for that view.
- Scene-cache resource creation failure disables VSM for all views of that scene for the frame.
- A missing backend capability returns disabled results rather than exposing stale resources.
- A page coordinate outside the packed-key range remains unmapped.
- Physical pool exhaustion leaves remaining requests unmapped and records a rate-limited warning.
- A failed shadow preparation never reuses a partially finalized page-table result as if it were current.

Forward shading uses the existing coarser-level fallback where available. If no valid level is mapped, the pixel uses the existing no-shadow fallback.

No round-robin scheduling, automatic resolution bias, or dynamic degradation policy is added. Pool capacity, clipmap levels, per-level resolution, and request limits are configured so normal workloads fit.

## Diagnostics

Diagnostics are reported per scene and per view:

```text
scene:
  physicalCapacity
  residentPages
  pinnedPages
  allocationFailures
  overflowFrames

view:
  viewID
  pageTableSlice
  requestedPages
  cacheHits
  allocatedPages
  dirtyPages
  projectionRevision
```

Existing VSM visualization reads the selected view's page-table slice while displaying the common physical atlas. Debug output must include `viewID` so identical absolute XY coordinates from different views are distinguishable.

## Verification

Focused automated verification covers:

- Stable nonzero `viewID` assignment and non-aliasing.
- Page-key packing and unpacking, including signed page coordinates.
- Confirmation that the key contains `viewID`, level, X, and Y but no epoch.
- Clipmap movement smaller than one page retaining the same origin.
- Page-aligned origin movement preserving keys for overlapping absolute pages.
- Incompatible projection changes invalidating only the affected view.
- Directional-light basis changes invalidating every directional view in one scene cache.
- Separate page-table slices for multiple views.
- One physical pool shared by views of one scene.
- Separate physical pools for different scenes.
- Family-order and coarse-to-fine request processing without round-robin.
- Current-family pinning preventing a later view from evicting an earlier view's requested pages.
- Pool exhaustion leaving excess requests unmapped.
- One-view Player compatibility.

Windows build and regression verification use:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Rendering acceptance launches the Editor directly with the project:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Acceptance checks:

- Scene View and Game View render from one family and one scene physical pool.
- Each view samples only its own page-table slice.
- A static camera converges to cache hits.
- Smooth camera panning requests narrow edge strips rather than redrawing all pages.
- Moving a caster invalidates its previous and current coverage.
- A later family view cannot corrupt an earlier view's mappings.
- D3D11 and D3D12 both render valid shadows.
- A disabled VSM path still renders the scene without shadows.

Metal remains disabled until native compute and the direct-RHI preparation path are supported and verified on macOS.
