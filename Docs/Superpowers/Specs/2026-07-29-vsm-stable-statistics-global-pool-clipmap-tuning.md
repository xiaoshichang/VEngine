# VSM Stable Statistics, Global Pool, And Clipmap Tuning Design

## Goal

Correct stable-frame VSM statistics, keep the VSM GPU allocation alive when Editor Play replaces the active `RTScene`, and reduce the current demo scene's steady request footprint from roughly 704 pages to roughly 100 pages.

## Confirmed Scope

- Stable frames report the requests represented by the retained page table as cache hits and report no allocation, redraw, or unmapped work.
- `VirtualShadowManager` owns one shared GPU resource pool for its lifetime.
- Replacing the active `RTScene` reuses the pool's atlas, sampler, page tables, request buffers, physical-page metadata, and statistics buffer.
- Replacing the active `RTScene` still resets logical page contents. This change does not reuse shadow contents across unrelated scenes.
- VSM keeps four clipmap levels and the existing 128-by-128 logical-page address space per level.
- Clipmap world coverage and page world size are enlarged together. Atlas extent and physical page size do not change.
- Metal remains fail-fast because VSM is not implemented there.

## Approaches Considered

### Selected: Manager-Owned Pool With Scene-Local Tracking

Move GPU resources and the page-table slice allocator into one manager-owned cache object. Keep each `SceneEntry` limited to invalidation tracking, reset state, and statistics identity. On an `RTScene` transition, reset the shared logical cache before the new scene uses it.

This removes repeated large GPU allocations while keeping scene transitions correct and preserving the current one-active-scene rendering model.

### Rejected: Reuse Page Contents Across Editor And Play Scenes

This would avoid both allocation and redraw work, but current render-item IDs are newly allocated during scene deserialization. Without stable caster identities or a verified scene-content signature, retaining those pages could sample stale shadows.

### Rejected: Keep One Complete Cache Per `RTScene`

This preserves independent scene contents but recreates a 4096-by-4096 depth atlas and all supporting buffers whenever Play creates a new `RTScene`. It is the source of the avoidable resource-allocation spike.

## Architecture

`VirtualShadowManager::Impl` owns a single `VirtualShadowSceneCache`. The cache name remains unchanged for this focused change, but its responsibility becomes the manager-wide physical resource pool and logical page-table storage.

`SceneEntry` retains:

- a weak `RTScene` reference;
- `VirtualShadowInvalidationTracker`;
- per-scene reset state;
- statistics identity.

The global cache retains:

- atlas texture and comparison sampler;
- page marks and dense page table;
- request list and request counts;
- physical-page metadata and statistics;
- the view slice allocator;
- a pending-reset flag.

When the active scene pointer differs from the scene that last used the pool, the manager:

1. marks the shared cache for reset;
2. clears page-table slices associated with registrations from the previous scene;
3. registers the current views against the shared slice allocator;
4. runs the normal reset/request/allocation/raster/finalize path using the existing GPU objects.

The manager does not create a second atlas or supporting buffer set.

## Stable-Frame Statistics

The persistent GPU statistics buffer currently contains the last update frame's counters. A stable frame skips request and residency work, so copying that buffer produces frozen first-frame values.

Before statistics readback, a stable-frame statistics pass writes current reuse semantics:

- `allocatedPhysicalPages`: preserve the last finalized allocation count;
- `requestedPages`: preserve the retained request count;
- `cachedPages`: equal `requestedPages`;
- `newlyAllocatedPages`: zero;
- `redrawnPages`: zero;
- `unmappedPages`: zero.

This pass is intentionally small and does not regenerate requests or scan physical pages. It updates only the six statistics words, declares the statistics buffer as a read-write FrameGraph dependency, and executes before the existing readback pass.

## Clipmap Tuning

Introduce one named clipmap coverage scale instead of changing atlas or page constants. Apply the scale consistently to each level's `worldRadius`; `pageWorldSize` continues to be derived from the scaled radius and the fixed logical-page dimension.

The starting scale is derived from the current request ratio:

`sqrt(704 / 100)`, approximately `2.65`.

Use a readable initial value of `2.5`, then measure the current demo scene. Adjust only this scale until a stationary scene reports approximately 80 to 120 requested pages. This range acknowledges that request generation includes a 3-by-3 neighbor guard band and overlapping clipmap levels, so request count is not a perfect inverse-square function.

The shadow distance remains the culling and depth-range input. Enlarging the radius/page size trades spatial shadow resolution for fewer physical-page requests; that trade is intentional for the current simple configuration.

## Failure Handling

The existing fail-fast VSM policy remains:

- invalid resource configuration, missing required resources, invalid view IDs, or unsupported Metal execution logs an error and terminates;
- no placeholder resources, transactions, fallback caches, or recoverable allocation paths are introduced.

## Verification

Because repository policy does not retain VSM unit tests by default, implementation uses temporary focused red/green diagnostics and removes them before completion.

Final verification consists of:

1. a Windows MSVC debug build;
2. the registered Windows CTest suite;
3. a D3D11 Editor smoke launch with `--project D:\github-desktop\VEngine\DemoProject`;
4. a stationary non-Play observation showing cached requests and zero redraw after warm-up;
5. an Editor-to-Play observation confirming no second VSM atlas/buffer allocation and measuring the first Play-frame page redraw;
6. a stationary request count in the 80-to-120 target range.

The Play transition may still contain the one-time logical cache reset and scene serialization/deserialization cost. Eliminating cross-scene redraw is explicitly outside this design.
