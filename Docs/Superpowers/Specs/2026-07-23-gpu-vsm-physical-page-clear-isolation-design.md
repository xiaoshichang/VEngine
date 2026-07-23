# GPU VSM Physical Page Clear Isolation Design

## Problem

GPU VSM clears every requested dirty physical page before drawing shadow casters. The current instanced clear draw emits one oversized triangle per physical page with local vertices `(0, 0)`, `(0, 2)`, and `(2, 0)`.

That triangle covers the intended square page, but it also extends into the physical slots to the right and below. The draw uses one atlas-wide viewport and scissor, so no per-instance raster state clips the triangle back to its own slot.

When a dirty page is cleared, triangular portions of neighboring clean pages can therefore be overwritten with depth `1.0`. Those neighboring pages remain marked clean and are not redrawn. Sampling them produces the triangular, V-shaped, and stepped shadow gaps seen after camera movement. More page allocation and invalidation makes the corruption more frequent, which explains why Play followed by Stop made the symptom easier to trigger.

## Selected Approach

Replace the oversized three-vertex triangle with an exact six-vertex rectangle per physical page:

```text
(0,0) ------ (1,0)
  | \          |
  |   \        |
  |     \      |
(0,1) ------ (1,1)
```

The rectangle is composed of two triangles and maps only to the current physical slot. The render pass remains one instanced draw across all physical pages.

This approach is preferred over:

- adding four clip distances to the oversized clear triangle, which introduces unnecessary shader clipping semantics for a simple rectangle;
- issuing one draw and scissor change per dirty page, which increases CPU and command-submission overhead.

Increasing the clear geometry from three to six generated vertices per physical page is negligible compared with caster rendering and does not change the GPU-driven allocation architecture.

## Implementation

- Define a shared `VirtualShadowPageClearVertexCount` constant with value `6`.
- Update the page-clear vertex shader to generate two exact triangles from `SV_VertexID`.
- Update the instanced clear draw to use the shared vertex count.
- Keep the existing physical-page flag test: only pages with both Dirty and Requested flags produce visible clear geometry.
- Do not change caster clipping, page allocation, cache revision handling, or sampling.

## Testing

Add focused unit coverage that the shared page-clear vertex count is six. The Editor shader-compilation path verifies that the corresponding six-entry HLSL vertex table is valid, while the visual regression verifies that the generated coordinates stay inside the physical slot and no longer erase neighboring pages.

Run:

- `VEngineVirtualShadowTests`;
- the complete Windows test preset;
- the D3D12 Editor regression:
  1. open `DemoProject`;
  2. rotate and translate Scene View across page boundaries before Play;
  3. enter Play long enough for physics objects to move;
  4. stop Play;
  5. rotate and translate Scene View again;
  6. verify that sphere and cube shadows contain no triangular, V-shaped, or stepped missing regions.

Stationary frame rate should remain at the existing baseline because the fix does not add draw calls, compute dispatches, or page allocations.
