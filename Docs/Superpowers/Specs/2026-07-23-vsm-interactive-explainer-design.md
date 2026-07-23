# VEngine GPU-Driven VSM Interactive Explainer Design

## Goal

Create a browser-based interactive explainer for VEngine's current GPU-driven Virtual Shadow Maps implementation. The explainer must make one frame's data flow understandable without requiring the reader to inspect the rendering code.

The primary path is the D3D11/D3D12 GPU-driven implementation. The CPU implementation appears only as a compact failure fallback.

## Audience And Scope

The intended audience is an engine programmer or technical artist who understands basic rendering concepts but may not know VEngine's VSM implementation.

The explainer covers:

- Directional-light clipmaps.
- Receiver depth reconstruction.
- Logical-page marking and request compaction.
- Persistent physical-page caching.
- Coarse-to-fine cache resolution and allocation.
- Dirty-page rendering into the physical depth atlas.
- Dense page-table lookup, coarse fallback, and 3-by-3 PCF.

It does not attempt to explain point-light shadows, sparse hardware resources, asynchronous compute, GPU Scene, or the complete CPU fallback algorithm.

## Presentation Model

Use a single-screen story player:

1. A clickable ten-step timeline.
2. A compact current-step header with its CPU, GPU Render, or GPU Compute label.
3. One dominant animated stage whose diagram changes for the selected step.
4. A row of three to five selectable substeps.
5. One compact input/output detail area for the selected substep.
6. Previous, replay, play/pause, and next controls.

Initial rendering must already communicate the first step. Animation never loops automatically. The user can navigate directly to any step, replay the current step, or let the player advance through substeps.

## Ten Main Steps

### 1. Prepare The Frame And Clipmaps

Primary processor: CPU.

Substeps:

1. Read camera, main directional light, render items, and per-view cache state.
   - Input: camera transform and projection, light direction and settings, opaque item snapshots, previous view cache.
   - Output: validated frame preparation input.
2. Build four snapped directional-light clipmaps.
   - Input: camera transform, light direction, shadow distance.
   - Output: four clipmap origins, page world sizes, depth epochs, and light basis.
3. Compare caster snapshots with the previous frame.
   - Input: stable item IDs, revisions, old bounds, and new bounds.
   - Output: local invalidation keys or a full-reset condition.
4. Ensure the atlas and persistent GPU buffers exist.
   - Input: RHI device, view name, atlas extent.
   - Output: atlas, sampler, page marks, dense page table, request list/counts, and physical-page metadata buffers.
5. Produce the GPU frame packet and constants.
   - Input: clipmaps, inverse view-projection, invalidations, screen size, cache-reset flag.
   - Output: `VirtualShadowFramePacket` and `VirtualShadowGpuConstants`.

Interaction: drag the camera. The clipmap grids move only when their page-sized snapping thresholds are crossed.

### 2. Render Receiver Depth

Primary processor: GPU Render.

Substeps:

1. Select opaque visible receiver meshes.
   - Input: opaque render queue and mesh resources.
   - Output: receiver draw sequence.
2. Transform mesh vertices through object and view-projection matrices.
   - Input: vertex positions, object constants, view constants.
   - Output: clip-space receiver geometry.
3. Perform depth testing.
   - Input: rasterized fragments and current depth.
   - Output: nearest receiver depth per covered pixel.
4. Publish the depth texture for compute access.
   - Input: completed depth attachment.
   - Output: readable `VirtualShadowReceiverDepth`.

Interaction: scrub between scene geometry and its resulting receiver depth texture.

### 3. Clear Requests And Apply Invalidation

Primary processor: GPU Compute.

Substeps:

1. Clear logical page marks and the dense page table.
   - Input: persistent page marks and page-table buffers.
   - Output: zeroed current-frame logical state.
2. Clear the four request counters.
   - Input: previous request counts.
   - Output: four zero counters.
3. Reset the physical cache when compatibility changed.
   - Input: reset flag and physical-page metadata.
   - Output: empty cache when reset is active.
4. Match local invalidation keys against resident physical pages.
   - Input: invalidation keys and resident page identities.
   - Output: affected pages marked Dirty.
5. Clear only the current-frame Requested flag.
   - Input: physical-page flags.
   - Output: cached identity, age, and Dirty state preserved across frames.

Interaction: switch between static, moved caster, and incompatible-light scenarios to see which state survives.

### 4. Mark Logical Pages From Receiver Depth

Primary processor: GPU Compute.

Substeps:

1. Load one receiver-depth pixel.
   - Input: screen coordinate and receiver depth texture.
   - Output: depth value or a rejected background pixel.
2. Reconstruct the world position.
   - Input: pixel UV, depth, inverse view-projection.
   - Output: receiver position in world space.
3. Choose the first eligible clipmap.
   - Input: camera-relative receiver depth and clipmap radii.
   - Output: finest applicable clipmap level.
4. Project into light space and quantize the logical page.
   - Input: world position, light basis, clipmap page size and origin.
   - Output: local logical-page coordinates.
5. Atomically mark that page in the selected and coarser levels.
   - Input: logical-page indices and page-marks buffer.
   - Output: sparse center-page marks.

Interaction: click a valid depth pixel. An animated ray reconstructs its world position, then lights the corresponding page in each eligible clipmap.

### 5. Expand And Compact Page Requests

Primary processor: GPU Compute.

Substeps:

1. Scan one logical page per compute thread.
   - Input: logical index in the 65,536-entry address space.
   - Output: level and local page coordinate.
2. Inspect the page's 3-by-3 mark neighborhood.
   - Input: sparse center-page marks.
   - Output: conservative requested/not-requested decision.
3. Atomically reserve an entry in the level counter.
   - Input: requested page and its clipmap level.
   - Output: unique per-level request index.
4. Append the logical page to its level segment.
   - Input: logical page and reserved request index.
   - Output: compact request list partitioned by clipmap level.

Interaction: play the neighborhood expansion, then watch scattered marks collect into four level-partitioned queues.

### 6. Resolve Physical-Page Cache Hits

Primary processor: GPU Compute.

Substeps:

1. Process clipmap levels from coarse L3 to fine L0.
   - Input: four request counts and request-list segments.
   - Output: one active level per resolve pass.
2. Convert a local logical page to its absolute page identity.
   - Input: logical index, snapped clipmap origin, depth epoch.
   - Output: packed absolute `key0` and `key1`.
3. Search persistent physical-page metadata.
   - Input: absolute key and physical-page pool.
   - Output: matching physical index or cache miss.
4. Bind a hit in the dense page table.
   - Input: logical index and matching physical index.
   - Output: `physicalIndex + 1` stored at the logical address.
5. Pin the hit and update its age.
   - Input: physical-page metadata and frame index.
   - Output: Requested flag and refreshed `lastUsedFrame`.

Interaction: select a request and watch its key probe the physical pool. Hits connect immediately to an existing atlas tile.

### 7. Allocate Or Evict Physical Pages

Primary processor: GPU Compute.

Substeps:

1. Skip requests already resolved by the hit pass.
   - Input: dense page-table entry.
   - Output: unresolved misses only.
2. Search for a free physical page.
   - Input: physical-page residency flags.
   - Output: free page candidate.
3. If necessary, select the oldest unpinned page.
   - Input: Requested flags and `lastUsedFrame`.
   - Output: bounded LRU victim or allocation failure.
4. Assign the absolute key and dense mapping.
   - Input: request identity and selected physical index.
   - Output: new physical metadata and logical-to-physical mapping.
5. Mark the new mapping Dirty and Requested.
   - Input: assigned physical page.
   - Output: page scheduled for shadow rendering.

Interaction: switch among cache hit, free allocation, LRU eviction, and all-pages-pinned failure scenarios.

### 8. Render Dirty Shadow Pages

Primary processor: GPU Render.

Substeps:

1. Instance a page-clear triangle across physical capacity.
   - Input: physical metadata and atlas layout.
   - Output: clear draw instance for every physical slot.
2. Clip inactive or clean instances.
   - Input: Resident, Dirty, and Requested flags.
   - Output: only Dirty+Requested atlas tiles remain.
3. Clear the active tiles to depth one.
   - Input: remapped fullscreen triangles.
   - Output: clean depth regions for pages that will be redrawn.
4. Instance every opaque caster across physical capacity.
   - Input: caster meshes, object transforms, physical-page metadata.
   - Output: caster/page instance pairs.
5. Clip irrelevant pairs and write page-local shadow depth.
   - Input: page bounds, transformed caster vertices, page projection.
   - Output: rendered depth in each active physical atlas tile.

Interaction: replay the tile clear, instance filtering, and page-local depth writes in sequence.

### 9. Finalize Physical-Page State

Primary processor: GPU Compute.

Substeps:

1. Visit every physical-page metadata entry.
   - Input: physical-page pool.
   - Output: one page state per compute thread.
2. Detect pages requested during this frame.
   - Input: Requested flag.
   - Output: rendered versus untouched branch.
3. Clear Dirty and Requested on rendered pages.
   - Input: rendered page flags.
   - Output: clean resident cached page.
4. Preserve unrequested cached pages.
   - Input: untouched identity, age, and Dirty state.
   - Output: reusable or still-invalidated cross-frame cache entries.

Interaction: advance to the next frame and observe that stable pages remain resident while their transient flags disappear.

### 10. Sample Shadows During Forward Shading

Primary processor: GPU Render.

Substeps:

1. Select the finest clipmap allowed by camera-relative depth.
   - Input: shaded world position, camera position and forward vector, clipmap radii.
   - Output: first sampling level.
2. Convert the shaded point to a logical-page address.
   - Input: biased world position, light basis, page size, clipmap origin.
   - Output: dense page-table index and page-local position.
3. Look up the physical page.
   - Input: dense page table.
   - Output: physical atlas index or missing entry.
4. Fall back to successively coarser levels when missing.
   - Input: missing fine mapping and remaining clipmap levels.
   - Output: first resident mapping or fully lit fallback.
5. Sample 3-by-3 PCF and shade the pixel.
   - Input: physical atlas tile, depth reference, comparison sampler, material lighting.
   - Output: filtered visibility and final lit color.

Interaction: move the shaded point, remove its fine mapping, and inspect the coarse fallback plus nine PCF taps.

## Visual Encoding

Use stable redundant labels:

- CPU: processor label plus one series color and square marker.
- GPU Render: processor label plus a second series color and triangle marker.
- GPU Compute: processor label plus a third series color and hexagonal marker.

Color is never the only encoding. Input data moves into a process node; output data moves away from it. Logical pages use grid cells, request lists use ordered slots, physical pages use atlas tiles, and page-table mappings use connecting lines.

## Interaction And Animation Rules

- Clicking a main step updates the stage, substep controls, and input/output detail together.
- Clicking a substep replays only that transition and updates the selected detail.
- Play advances through substeps and then to the next main step.
- Pause freezes the current state without resetting it.
- Replay restores the selected step's initial state and runs it again.
- Initial appearance is static and immediately useful.
- Motion is short, purposeful, and non-looping.
- `prefers-reduced-motion` disables spatial transitions while preserving state changes.
- All controls use native buttons and maintain keyboard tab order.

## Failure And Fallback Presentation

Failures are shown as compact branches rather than a second full workflow:

- Invalid/background receiver depth produces no page mark.
- Exhausted and fully pinned physical pools leave the dense entry missing; sampling tries coarser levels.
- GPU resource or pipeline setup failure disables GPU-driven VSM for the view and selects the CPU fallback on later frames.
- Incompatible light, device, resource shape, or shadow distance triggers a full GPU cache reset.

## Source Alignment

The explainer follows these implementation sources:

- `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- `Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.cpp`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- `Assets/Builtin/Shaders/BasicMesh.hlsl`
- `Docs/Superpowers/Specs/2026-07-23-gpu-driven-vsm-design.md`

The names used for passes, buffers, flags, and shaders should remain recognizable from those sources.

## Verification

Before delivery:

1. Confirm all ten main steps are directly navigable.
2. Confirm every main step contains three to five substeps.
3. Confirm every substep shows processor, input, and output.
4. Exercise previous, next, replay, play, pause, direct-step selection, and substep selection.
5. Exercise the interactive scenarios in steps 1, 4, 5, 6, 7, 8, and 10.
6. Check for JavaScript errors and undefined identifiers.
7. Verify the primary interaction updates the diagram and detail.
8. Inspect at approximately 736 px and 320 px widths without clipping or internal scrolling.
9. Verify keyboard operation, visible focus, accessible labels, and reduced-motion behavior.
10. Read the generated fragment back to ensure it contains literal markup and no escaped quote or newline artifacts.
