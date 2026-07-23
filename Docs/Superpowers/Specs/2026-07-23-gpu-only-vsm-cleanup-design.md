# GPU-Only Virtual Shadow Maps Cleanup Design

## Goal

Remove VEngine's CPU Virtual Shadow Maps implementation and retain only the GPU-driven directional-light VSM path.

After this cleanup:

- D3D11 and D3D12 use the GPU-driven VSM path.
- Metal has no VSM implementation until native GPU-driven support is added.
- A GPU VSM resource, pipeline, or runtime failure disables VSM for the affected view while allowing scene rendering to continue.
- No CPU VSM implementation, build entry, test, or documentation remains.

## Runtime Architecture

`BaseRenderer` prepares only GPU-driven virtual-shadow frame data. It does not select a backend-specific CPU path and does not retry with CPU work after GPU resource preparation fails.

`GpuVirtualShadowRenderPass` remains the sole VSM rendering pass. A failure marks GPU VSM unavailable for the view, disables the current VSM packet, and leaves the remaining scene passes active. Subsequent frames keep VSM disabled for that view until the view binds to a different RHI device and recreates its GPU resources.

The following shared GPU requirements remain:

- Clipmap construction and absolute virtual-page keys.
- Dynamic-caster invalidation tracking.
- GPU page-mark, request, physical-page metadata, dense sampling-table, and indirect-work buffers.
- GPU VSM constants, forward sampling, physical-page rendering, and per-view atlas ownership.

## Code Removal

Delete the CPU-only implementation:

- `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.h`
- `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.cpp`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.cpp`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.cpp`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.cpp`

Remove their CMake source entries, renderer members, frame-graph registration, public APIs, frame-packet fields, and types used only by CPU request generation or CPU page residency.

Keep `VirtualShadowViewCache`, but reduce it to GPU frame preparation, shared invalidation tracking, atlas/sampler ownership, GPU resource ownership, and GPU availability state.

## Platform And Failure Behavior

Metal must not silently substitute another shadow implementation. `BaseRenderer` disables VSM when the active RHI backend cannot run the GPU-driven path.

On D3D11 or D3D12, failure to create or execute required GPU VSM resources or pipelines:

1. Logs a concise error through the VEngine logging facade.
2. Disables VSM for the current view.
3. Keeps opaque, transparent, editor, and presentation rendering active.
4. Never calls CPU request generation, CPU cache allocation, CPU page drawing, or CPU page-table upload.

Forward passes retain a valid non-shadow sampling binding when VSM is disabled so removal does not introduce invalid resource bindings.

## Tests

Remove CPU-only unit coverage for:

- CPU hash page-table insertion and collision behavior.
- CPU page-cache allocation, pinning, eviction, dirty state, and isolation.
- CPU receiver-bound page request construction.
- CPU `PrepareFrame`, dirty-page draw construction, and rendered-page bookkeeping.

Retain focused coverage for:

- Virtual-page key packing and validation needed by GPU buffers.
- Clipmap construction and numerical bounds.
- Dynamic-caster invalidation used by the GPU path.
- GPU frame preparation, invalidation payloads, reset behavior, and disabled-frame history.
- Physical atlas layout constants and GPU constant-buffer layout.

No replacement CPU compatibility tests are added.

## Documentation Cleanup

Delete the original CPU VSM design and implementation plan:

- `Docs/Superpowers/Specs/2026-07-21-virtual-shadow-maps-design.md`
- `Docs/Superpowers/Plans/2026-07-21-virtual-shadow-maps.md`

Update current architecture, development, GPU VSM design/plan, and presentation documentation to:

- Remove CPU fallback and Metal CPU-path claims.
- State that GPU-driven VSM is the only implementation.
- State that Metal currently has no VSM.
- State that GPU failures disable VSM without aborting scene rendering.

Update the GPU-driven VSM workflow presentation so it no longer depicts or recommends a CPU fallback path. Historical documentation unrelated to the CPU VSM implementation remains unchanged.

## Verification

The cleanup is complete when:

1. Repository search finds no references to the deleted CPU classes, files, CPU VSM fallback, or Metal CPU VSM path.
2. CMake configures successfully through `CMake/Scripts/WithMsvc.bat`.
3. The Windows debug build succeeds.
4. Relevant VSM and renderer CTest targets pass.
5. D3D11 and D3D12 smoke checks show the GPU-driven path without CPU fallback messages.
6. Documentation and the presentation consistently describe GPU-only VSM behavior.
