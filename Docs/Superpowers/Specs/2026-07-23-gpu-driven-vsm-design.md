# GPU Driven Virtual Shadow Maps Design

## Goal

Upgrade VEngine's existing directional-light Virtual Shadow Maps implementation from CPU receiver-bound requests and CPU page draw construction to a same-frame GPU-driven path:

```text
camera depth
  -> GPU receiver-page marking
  -> GPU physical-page allocation and page-table construction
  -> GPU dirty-page metadata
  -> page-local shadow rendering
  -> forward shadow sampling
```

The existing CPU path remains available as a compatibility fallback. The GPU path is enabled on D3D11 and D3D12; Metal continues to use the CPU path until native compute encoding is completed and verified on macOS.

## Implemented Slice

The July 2026 implementation delivers the same-frame Windows path:

- An opaque receiver depth prepass.
- One GPU thread per depth pixel for world reconstruction and page marking.
- Dense logical request and page-table buffers owned per render view.
- A persistent physical-page metadata buffer with cache hits and bounded LRU eviction.
- GPU page clear and caster rendering instanced across physical-page capacity, without page-request readback.
- Forward dense-page-table sampling with the original CPU hash-table fallback.
- D3D11 and D3D12 runtime acceptance through the DemoProject Editor path.

The correctness-first allocator processes compact requests one clipmap level at a time after parallel cache-hit resolution. Dynamic caster revisions upload bounded local invalidation keys, while incompatible light/resource changes reset the GPU cache. Profiling counters, page debug views, transient FrameGraph buffers, and native Metal compute remain follow-up work.

## Existing State

VEngine already provides:

- Four directional-light clipmap levels.
- A nominal 16K logical VSM per level.
- 128-by-128 virtual and physical pages.
- Per-view physical depth atlases.
- Stable absolute virtual page keys.
- CPU LRU residency and invalidation.
- Page-local depth rendering.
- Forward shader page-table lookup, coarse-level fallback, and PCF.
- Per-view cache isolation.

The missing prerequisites are:

- Compute shader modules and compute pipelines.
- Storage/UAV buffers and resource bindings.
- Compute dispatch commands.
- Buffer resources and compute passes in FrameGraph.
- A camera depth prepass before VSM page requests.
- GPU request bitsets, page-table buffers, counters, and page metadata.
- A page rendering path that does not require CPU readback of the GPU request list.

## Scope

The first GPU-driven implementation supports:

- One shadow-casting directional light.
- Four clipmap levels.
- Opaque rigid mesh receivers and casters.
- One GPU request bit per logical page.
- Same-frame page marking from the current camera depth.
- GPU allocation into the existing per-view physical atlas.
- GPU-visible dense page tables for forward shadow sampling.
- GPU page metadata used by page clear and caster rendering.
- D3D11, D3D12, and Metal through common RHI contracts.
- CPU fallback when GPU-driven resources or pipelines cannot be created.

The first implementation does not add:

- Point-light or spot-light VSMs.
- Hardware sparse textures.
- Async compute queues.
- Bindless mesh rendering.
- Mesh shaders.
- SMRT soft shadows.
- Alpha-tested, skinned, or world-position-offset casters.
- Cross-view physical page sharing.

## RHI Additions

### Shader And Pipeline

Add `RhiShaderStage::Compute`, `RhiComputePipelineDesc`, and `RhiDevice::CreateComputePipeline`.

Graphics and compute use separate `RhiPipelineState` and `RhiComputePipelineState` interfaces. A compute pipeline has exactly one compute shader and a resource layout.

### Buffers

Extend `RhiBufferUsage` with:

```text
Storage
Indirect
Readback
```

Add a structured element stride to `RhiBufferDesc`. A storage buffer may be bound as read-only or read-write:

```text
RhiPipelineResourceKind::StorageBuffer
RhiPipelineResourceKind::ReadWriteStorageBuffer
```

The first slice does not require storage textures. The physical atlas remains a depth attachment plus sampled texture.

### Commands

Add:

```text
SetStorageBuffer(stage, slot, buffer, offset, size)
SetReadWriteStorageBuffer(stage, slot, buffer, offset, size)
Dispatch(groupX, groupY, groupZ)
DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)
```

All compute work records on the existing graphics command list and graphics queue. D3D11 maps to the immediate context, D3D12 maps to the direct command list, and Metal switches between compute and render encoders on one command buffer.

## FrameGraph Additions

FrameGraph gains imported logical buffer resources and compute passes:

```text
ImportBuffer
Read
Write
AddComputePass
```

A pass node has one type:

```text
Raster
Compute
```

Raster validation keeps its attachment requirements. Compute passes require no attachment and execute without opening a native render pass.

The graph tracks buffer versions with the same producer/reader dependency rules as textures. VSM buffers are persistent per-view resources and are imported each frame. Transient buffer creation, aliasing, and pooling are deferred.

## Per-View GPU Resources

Each `VirtualShadowViewCache` adds persistent buffers:

```text
pageMarks:
  4 levels * 128 * 128 uint entries = 256 KiB

densePageTable:
  4 levels * 128 * 128 uint entries = 256 KiB

physicalPageMetadata:
  one entry per atlas page

clipmapConstants:
  GPU marking and allocation constants
```

The dense page-table entry is one `uint`: zero means missing and `physicalPageIndex + 1` means resident. Residency, dirty, requested, and age flags live in the physical metadata.

Physical metadata contains:

- The owning clipmap level and logical page coordinate.
- The atlas tile coordinate.
- The page view-projection matrix.
- The light-space page bounds.
- Residency and dirty flags.

## Same-Frame Pass Order

```text
CameraDepthPrepass
  -> VirtualShadowResetPass
  -> VirtualShadowMarkPagesPass
  -> VirtualShadowAllocatePagesPass
  -> VirtualShadowDepthPass
  -> OpaqueScenePass
  -> TransparentScenePass
```

`CameraDepthPrepass` writes the renderer depth texture. The opaque scene pass loads that depth instead of clearing it.

## Page Marking

`VirtualShadowMarkPagesCS` dispatches one thread per camera pixel in 8-by-8 groups.

For each non-background depth sample:

1. Reconstruct world position with inverse view-projection.
2. Transform to directional-light space.
3. Mark every clipmap level whose snapped working region covers the receiver; allocation visits coarse levels first so pressure always leaves a valid fallback before spending remaining pages on finer levels.
5. Convert the light-space XY coordinate to the selected 16K virtual texel coordinate.
6. Divide by 128 to obtain a local logical page coordinate.
7. Atomically OR the page bit in `requestBits`.
8. Conservatively mark neighboring pages when the configured filter footprint crosses a page boundary.

The first implementation uses global atomic OR directly. Wave-level deduplication is a measured follow-up, not a prerequisite.

### Request Compaction Optimization

The first performance iteration keeps the same conservative requested-page set while moving repeated work out of the per-pixel pass:

1. Select the first clipmap level with the same camera-depth rule used by forward sampling.
2. Mark only the center logical page for the selected level and its coarser fallback levels.
3. Dispatch one thread per logical page to test the 3-by-3 neighborhood of center-page marks.
4. Append each resulting unique request into a level-partitioned request list with four atomic counters.
5. For each level from coarse to fine, resolve resident-page hits in parallel and then allocate only that level's misses serially. This preserves fallback priority without paying a serial cache lookup for every hit.

This reduces global page-mark atomics from at most 36 to at most 4 per receiver pixel, preserves the one-page conservative border, and preserves coarse-level allocation priority. The per-level ordering is required: resolving all levels at once would allow fine cached pages to pin the pool before coarse misses. Dirty-page draw compaction and indirect caster submission remain a separate performance iteration.

## Allocation

`VirtualShadowResolvePageHitsCS` and `VirtualShadowAllocatePagesCS` run as a coarse-to-fine pair for each clipmap level and consume only that level's compact request segment.

For each requested page:

1. Check the persistent dense page-table entry.
2. If the mapping is valid and its absolute page identity still matches the current clipmap origin, retain the physical page.
3. Otherwise obtain a free physical page from the allocator state.
4. Write the dense mapping and physical metadata.
5. Mark a new or invalid mapping dirty.

Existing CPU invalidation results are uploaded as compact absolute page keys during the transition. The request-clear pass marks matching resident pages dirty before cache-hit resolution. This preserves current transform/add/remove correctness without immediately duplicating invalidation tracking on GPU.

When the pool is full, allocation evicts the oldest physical page not already requested by the current frame. If every page is pinned, the entry remains invalid and forward sampling continues with coarser-level fallback.

## Page Rendering Without CPU Readback

The GPU does not read page requests back to the CPU.

The depth pass uses instanced page rendering:

- Page-clear geometry is instanced across physical page capacity. Inactive instances clip out; dirty instances remap a fullscreen triangle to their atlas tile and write depth 1.
- Each opaque caster draw is issued once with instance count equal to physical page capacity.
- The VSM caster vertex shader reads physical page metadata by `SV_InstanceID`.
- Inactive or non-intersecting caster/page instances clip out.
- Active instances apply the page view-projection matrix and remap clip XY into the target atlas tile.

This is intentionally a correctness-first bridge until VEngine has a general GPU scene and indirect mesh draw pipeline. It removes CPU page readback and CPU per-page draw construction while keeping the existing per-mesh RHI resources.

## Forward Sampling

Forward shading recomputes:

```text
clipmap level
local logical page coordinate
page-local texel offset
```

It indexes the dense GPU page table instead of the existing constant-buffer hash table. The physical atlas addressing and gutter-safe PCF remain unchanged.

The CPU hash table remains compiled for fallback mode.

## Cache And Invalidation

- Clipmap origins remain snapped to page-world increments.
- A page-table entry stores enough absolute identity to detect when the same local address now represents a different world page.
- New mappings are dirty.
- Moving, added, removed, or newly shadow-casting opaque casters invalidate only pages overlapped by their previous or current world bounds.
- Light-direction, shadow-distance, device, or GPU resource-shape changes reset GPU mappings because the cached page projection is no longer compatible.
- Camera cuts rebuild current-frame requests but retain compatible absolute page mappings.
- Clean requested mappings are reused without redraw.
- Unrequested mappings remain cache candidates.
- Allocation uses a GPU free list first and then an age-based unpinned victim.

The initial allocator may scan the bounded physical metadata array to select the oldest non-requested page. Atlas capacities are small enough for a correctness-first implementation; hierarchical eviction is deferred until profiling justifies it.

### Dynamic Caster Local Invalidation

The GPU path reuses `VirtualShadowInvalidationTracker`, which already provides the CPU VSM path with old/new caster-bounds invalidation:

1. `PrepareGpuFrame` builds `VirtualShadowCasterSnapshot` values for opaque shadow casters.
2. The tracker compares each render-item ID and revision with the previous frame.
3. A moved caster contributes page keys for both its previous bounds and current bounds, so the old shadow is erased and the new shadow is rendered.
4. A new caster contributes its current bounds. A removed or disabled caster contributes its last tracked bounds.
5. A light-direction change requests a full mapping reset because absolute light-space page identities no longer describe the same world regions.

The frame packet carries the compact invalidated-key list without GPU-to-CPU readback. GPU invalidation keys cover the caster bounds' absolute XY pages at every clipmap level without clipping them to the current camera working region. Matching uses absolute XY plus clipmap level and deliberately ignores the current depth epoch. This ensures that a clean resident page remains invalidated when its caster changes while the camera is elsewhere, including when the camera later returns to an older origin or depth epoch.

GPU mode reuses the otherwise-unused constant-buffer page-entry array for up to `VirtualShadowPageTableCapacity` invalidation keys. The fourth scalar after `resetCache`, `gpuDriven`, and `passLevel` contains the invalidation count. If the key count exceeds the fixed capacity, a sentinel requests full resident-content invalidation while preserving compatible mappings.

During `VirtualShadowClearRequests`, each valid physical page:

- clears its current-frame requested flag;
- keeps its key, age, and dirty state unless a true mapping reset is active;
- sets its dirty flag when its absolute key appears in the invalidation list;
- sets its dirty flag unconditionally for the overflow sentinel.

Dirty, unrequested pages remain dirty in the persistent cache. They are cleared only after a later frame requests and renders them. This avoids both stale shadows and unnecessary rendering outside the visible requested set.

This is intentionally a hybrid invalidation stage: the CPU supplies a small scene-change list while page marking, cache lookup, allocation, and rendering remain GPU driven. A future GPU Scene can replace the CPU tracker with GPU bounds comparison without changing the physical-page cache contract.

Compatibility and caster history are committed only after the frame has an enabled light, valid clipmaps, and an invertible view-projection matrix. Disabled or otherwise unsubmitted frames therefore cannot consume shadow-distance or caster changes before a GPU clear pass can receive them.

## Failure Handling

- Compute, storage-buffer, or pipeline creation failure disables GPU shadows without aborting scene rendering and selects the CPU VSM path for subsequent frames on that view.
- Invalid depth or inverse view-projection disables GPU page marking for the frame.
- Page-pool exhaustion produces missing entries and coarse fallback.
- GPU resource-shape changes rebuild only the affected view cache.
- D3D11/Metal backend compilation failures do not silently select a partially initialized GPU path.

## Diagnostics

Planned per-view counters:

```text
pixelsVisited
pagesMarked
cacheHits
pagesAllocated
pagesEvicted
pagesDirty
allocationFailures
```

Add visualization modes for:

- Selected clipmap level.
- Requested logical pages.
- Dense page-table residency.
- Dirty versus cached physical pages.

## Verification

Windows automated verification:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Windows rendering acceptance:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Verify D3D11 and D3D12 separately:

- Static camera converges to cached pages.
- Smooth camera movement adds narrow edge strips.
- Camera cuts do not show persistent missing shadows.
- Moving a caster invalidates its old and new coverage.
- Scene View and Game View retain isolated page pools.
- CPU fallback remains functional.

Metal changes must compile and run on macOS before the feature is considered cross-platform complete.
