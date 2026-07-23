# GPU Driven Virtual Shadow Maps Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace CPU receiver-page requests and CPU per-page draw construction with a same-frame GPU-driven directional-light VSM path while preserving the existing CPU implementation as a fallback.

**Architecture:** Add compute and storage-buffer contracts to the common RHI and FrameGraph, generate current-frame VSM requests from a camera depth prepass, maintain per-view GPU page tables and physical metadata, and render dirty pages through page-instanced caster draws without CPU readback. Roll out through D3D11 and D3D12 on Windows, keep Metal API mappings in the common design, and retain CPU VSM fallback until backend acceptance succeeds.

**Tech Stack:** C++20, CMake, HLSL Shader Model 5/6, D3D11, D3D12, Metal, VEngine FrameGraph and RHI.

**Implementation result (2026-07-23):** Tasks 1, 2, 4–10, and Windows portions of Task 12 are implemented. Task 3 is intentionally limited to common-interface and instanced-draw parity; Metal stays on CPU VSM until native compute encoding is implemented on macOS. Task 11 diagnostics and fine-grained GPU invalidation are deferred and recorded in `Docs/DevelopmentPlan.md`.

---

## File Structure

### Common RHI

- Modify `Engine/RHI/Common/RhiTypes.h`: compute stage, storage/indirect usages, compute pipeline descriptors, buffer element stride.
- Modify `Engine/RHI/Common/RhiDevice.h`: compute pipeline creation, storage bindings, dispatch, instanced draws.
- Modify `Engine/RHI/D3D11/D3D11Rhi.cpp`: structured buffers, SRV/UAV views, compute pipeline, dispatch, instanced draw.
- Modify `Engine/RHI/D3D12/D3D12Rhi.cpp`: structured buffers, SRV/UAV descriptors, compute root signatures and PSOs, UAV barriers, dispatch, instanced draw.
- Modify `Engine/RHI/Metal/MetalRhi.mm`: storage buffers, compute pipeline/encoder, dispatch, instanced draw.

### FrameGraph

- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h`: logical buffer descriptors and handles.
- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`: buffer creation/import and compute-pass registration.
- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h/.cpp`: buffer reads/writes.
- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`: typed pass execution and buffer dependency/lifetime handling.
- Modify `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphTransientResourcePool.h/.cpp`: transient buffer pooling.

### Renderer And VSM

- Create `Engine/Runtime/Render/Renderer/RenderPass/CameraDepthPrepass.h/.cpp`: current-view opaque depth.
- Create `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowGpuPagePass.h/.cpp`: reset, mark, and allocate compute passes.
- Modify `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.h/.cpp`: page-instanced clear and caster draws.
- Modify `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`: load prepass depth and bind dense GPU page table.
- Modify `Engine/Runtime/Render/Renderer/StandaloneRenderer.h/.cpp`: register the new pass order.
- Modify `Engine/Runtime/Render/Renderer/BaseRenderer.h/.cpp`: prepare common clipmap/invalidation input while deferring page requests to GPU mode.
- Modify `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`: GPU constants, dense table entries, page metadata, counters.
- Create `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuResources.h/.cpp`: per-view persistent GPU buffers and pipeline mode.
- Modify `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h/.cpp`: GPU resource ownership and CPU fallback.
- Modify `Engine/Runtime/Render/RenderShaderIDs.h`: depth-prepass and GPU VSM shader IDs.
- Modify `CMake/Targets/Engine.cmake`: register new engine files.

### Verification

- Modify `Tests/Unit/VirtualShadowTests.cpp`: focused CPU-layout and clipmap-address invariants shared with shaders.
- Modify `Docs/DevelopmentPlan.md`: record GPU-driven VSM rollout and current backend acceptance.

## Task 1: Add Common Compute And Storage-Buffer RHI Contracts

- [ ] Add `Compute` to `RhiShaderStage`.
- [ ] Add storage-buffer resource kinds and buffer usage flags.
- [ ] Add `structureStride` to `RhiBufferDesc`.
- [ ] Define `RhiComputePipelineDesc`.
- [ ] Add compute pipeline creation, storage-buffer binding, `Dispatch`, and instanced indexed draw to the common interfaces.
- [ ] Compile the common engine contract and fix every backend override error before implementing native behavior.

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: compilation reaches backend implementations and reports only the deliberately unimplemented native mappings, then passes after Task 2.

## Task 2: Implement D3D11 And D3D12 Compute/Storage Support

- [ ] Create D3D11 structured-buffer SRV/UAV views according to usage.
- [ ] Compile and bind D3D11 compute shaders and state.
- [ ] Bind compute SRVs/UAVs with hazard unbinding.
- [ ] Implement D3D11 dispatch and instanced indexed draw.
- [ ] Create D3D12 structured-buffer SRV/UAV descriptors.
- [ ] Build D3D12 compute root signatures from resource layouts.
- [ ] Create D3D12 compute PSOs.
- [ ] Bind compute root resources and dispatch.
- [ ] Add UAV ordering barriers between dependent compute passes.
- [ ] Implement D3D12 instanced indexed draw.

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: all Windows targets compile.

## Task 3: Implement Metal Contract Parity

- [ ] Create Metal storage buffers with appropriate resource options.
- [ ] Create `MTLComputePipelineState`.
- [ ] Switch from render to compute encoder before dispatch.
- [ ] Bind read-only and read-write buffers to compute slots.
- [ ] End the compute encoder before the next raster pass.
- [ ] Implement instanced indexed drawing.

Verification: compile through the `mac-debug` preset on macOS. Windows code must keep Objective-C and Metal types out of common headers.

## Task 4: Add FrameGraph Buffers And Compute Passes

- [ ] Define versioned logical buffer handles and descriptors.
- [ ] Add imported and transient buffer backings.
- [ ] Add buffer read/write access records.
- [ ] Add `AddComputePass`.
- [ ] Validate raster and compute pass declarations independently.
- [ ] Execute compute passes without a render pass.
- [ ] Pool transient buffers.
- [ ] Add focused FrameGraph tests only if existing test infrastructure can exercise the new pure graph validation without an RHI device.

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: 9 existing tests plus any focused graph test pass.

## Task 5: Add Camera Depth Prepass

- [ ] Add depth-prepass shader IDs and shader compilation.
- [ ] Draw opaque rigid meshes into the renderer depth target.
- [ ] Register the prepass before GPU page marking.
- [ ] Change opaque rendering to load prepass depth.
- [ ] Preserve behavior for views without VSM by omitting the prepass unless GPU VSM is active.

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: Editor and Player compile for D3D11 and D3D12.

## Task 6: Add Per-View GPU VSM Resources

- [ ] Define C++/HLSL-compatible request, table, metadata, allocator, and statistics layouts.
- [ ] Add static assertions for size and alignment.
- [ ] Create per-view persistent GPU buffers.
- [ ] Rebuild resources when atlas or clipmap shape changes.
- [ ] Select CPU fallback when any required resource is unavailable.
- [ ] Preserve independent Scene/Game/Player resource ownership.

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: all tests pass.

## Task 7: Implement GPU Page Reset And Marking

- [ ] Add reset compute shader for request bits and counters.
- [ ] Add one-thread-per-depth-pixel marking shader.
- [ ] Reconstruct world position from depth.
- [ ] Select clipmap level from coverage and projected footprint.
- [ ] Convert to virtual page coordinates.
- [ ] Mark request bits with atomic OR.
- [ ] Mark the conservative filter-neighbor footprint.
- [ ] Register reset and mark passes with explicit graph dependencies.

Verification: add a debug visualization that displays selected clipmap level and requested pages.

## Task 8: Implement GPU Allocation And Dense Page Table

- [ ] Resolve requested bits into dense page-table entries.
- [ ] Reuse matching persistent physical metadata.
- [ ] Allocate from the free list and evict the oldest unrequested page under pressure.
- [ ] Apply uploaded CPU invalidation keys.
- [ ] Write page matrices, light-space bounds, atlas origins, and dirty flags.
- [ ] Preserve invalid entries on allocation failure for coarse fallback.
- [ ] Publish GPU counters.

Verification: stationary camera converges to cache hits; moving camera allocates only newly exposed strips.

## Task 9: Render Dirty Pages Without CPU Readback

- [ ] Replace CPU page-local clear loops in GPU mode with instanced page clears.
- [ ] Add storage-buffer access to the VSM caster vertex pipeline.
- [ ] Issue one instanced draw per caster across physical capacity.
- [ ] Clip inactive/non-intersecting page instances.
- [ ] Remap active clip coordinates to the physical atlas tile.
- [ ] Mark rendered page metadata clean after the depth pass.
- [ ] Retain the existing CPU dirty-page draw path for fallback mode.

Verification: requested physical pages contain stable shadow depth without CPU request-list readback.

## Task 10: Sample The Dense GPU Page Table

- [ ] Bind the dense page table and physical metadata to opaque and transparent forward shaders.
- [ ] Select the same clipmap and page address used by the marker.
- [ ] Resolve the physical page from the dense entry.
- [ ] Preserve gutter-safe PCF.
- [ ] Preserve coarser clipmap fallback.
- [ ] Keep the CPU hash-table shader path as a pipeline variant.

Verification: D3D11 and D3D12 produce equivalent shadows in the DemoProject.

## Task 11: Diagnostics And Documentation

- [ ] Expose page-marking/allocation counters per view.
- [ ] Add selected-level, requested-page, page-table, and physical-page debug modes.
- [ ] Rate-limit fallback and pool-pressure warnings.
- [ ] Update `Docs/DevelopmentPlan.md` and VSM design documentation.

## Task 12: Final Verification

- [ ] Run Windows unit tests.
- [ ] Build Editor and Player.
- [ ] Launch Editor with `--project D:\github-desktop\VEngine\DemoProject`.
- [ ] Verify D3D11.
- [ ] Verify D3D12.
- [ ] Verify Scene/Game cache isolation.
- [ ] Run `git diff --check`.
- [ ] Review the full diff against the design.

Commands:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
git diff --check
git status --short
```
