# GPU VSM Page-Clipping Artifact Fix Design

## Problem

Sphere shadows contain page-corner-shaped bright notches. The defect is visible on the first static frame and remains visible while the scene is not moving.

The issue reproduces on both D3D11 and D3D12. The sphere mesh is a closed manifold with 2,208 triangles and no boundary or non-manifold edges. The two straight edges of each notch follow the projected light-space page axes. These observations rule out dynamic invalidation, backend-specific raster state, and missing source geometry as the primary cause.

The remaining failure boundary is the common GPU virtual-shadow page caster. It maps every requested logical page into one physical-atlas slot and uses four `SV_ClipDistance` values to restrict caster geometry to that slot.

## Goals

- Produce continuous sphere-shadow silhouettes across logical page corners.
- Preserve the GPU-driven request, allocation, cache, and instanced page-rendering architecture.
- Preserve the existing one-draw-per-caster submission model.
- Keep D3D11 and D3D12 behavior consistent.
- Avoid a material frame-rate regression in editing and Play modes.

## Non-Goals

- Changing clipmap selection or page allocation policy.
- Increasing the physical atlas size or requested-page dilation.
- Returning to CPU-driven per-page draw submission.
- Adding soft-shadow filtering or changing the artistic appearance of valid shadow edges.
- Refactoring unrelated renderer or RHI code.

## Selected Design

Replace hardware vertex clip-distance page clipping in the GPU page caster with explicit fragment-stage clipping.

The page-caster vertex shader will continue to:

1. Transform the caster vertex into world and light space.
2. Compute logical `pageUv`.
3. Map the vertex into the physical atlas slot.
4. Compute normalized shadow depth.

Instead of writing `SV_ClipDistance0`, it will pass `pageUv` to a minimal depth-only fragment shader. The fragment shader will discard fragments outside the logical page plus the existing one-texel gutter. Fragments inside the page write depth through the existing depth-only pipeline.

This keeps the same physical-page instances, atlas layout, depth convention, and gutter size. Only the page-boundary rejection mechanism changes.

## Data Flow

```text
PhysicalPage instance
  -> decode logical page key
  -> transform caster vertex to light space
  -> compute pageUv and atlas position
  -> interpolate pageUv
  -> fragment clip against [-gutter, 1 + gutter]
  -> write page depth
  -> sample through the existing dense page table
```

No page-table or cache data structure changes are required.

## Pipeline Changes

`GpuVirtualShadowRenderPass` will compile and bind a dedicated page-caster fragment shader in addition to the existing vertex shader.

The page-caster graphics pipeline will:

- retain `SolidNoCullRasterizer`;
- retain `DepthReadWriteLessEqual`;
- retain the position-only vertex declaration;
- keep zero color attachments and `Depth32Float`;
- add the depth-only fragment shader;
- keep the current uniform and physical-page buffer bindings.

The fragment shader needs no resource bindings. Its only input is interpolated `pageUv`.

## Failure Handling

Shader or pipeline creation failure will continue through the existing `DisableGpuVirtualShadows` fallback path. The change must not introduce a partial mode where the page caster runs without its fragment shader.

The fragment clip limits must use the same `VirtualShadowPhysicalPageContentSize` and one-texel gutter convention as page sampling. The shader constants remain embedded in the current pass for this focused fix.

## Verification

The existing captured image is the failing visual baseline: a static sphere whose shadow crosses a logical page corner has a bright wedge bounded by the two page axes.

Verification will cover:

1. Compile the new vertex and fragment shaders through the normal Editor build.
2. Launch the DemoProject with D3D12 and confirm all static sphere silhouettes are continuous.
3. Launch the DemoProject with D3D11 and confirm the same result.
4. Enter Play mode and confirm moving shadows remain continuous while crossing page boundaries.
5. Pause after movement and confirm cached pages retain the correct result.
6. Compare editing and Play frame rates against the current 75 FPS and moving-scene baseline; reject the change if it causes a material regression.
7. Run the virtual-shadow unit test target and the relevant renderer/RHI tests.
8. Run `git diff --check`.

The implementation starts as a single-variable page-clipping change. If fragment clipping does not remove the reproduced notch, the experiment will be reverted and the issue will return to page-residency instrumentation rather than accumulating additional speculative fixes.

## Acceptance Criteria

- No page-corner-shaped holes are visible in any sphere shadow in the supplied DemoProject view.
- Shadow shapes remain correct in both static and moving scenes.
- D3D11 and D3D12 both render successfully without falling back from GPU-driven VSM.
- No page allocation, atlas-size, or CPU per-page submission workaround is introduced.
- No material frame-rate regression is observed.
