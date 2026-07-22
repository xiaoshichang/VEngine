# Virtual Shadow Physical Page Y Orientation Design

## Problem

Virtual shadow depth pages are rendered with light-space Y increasing upward. D3D viewport transformation writes NDC Y into a texture whose V coordinate increases downward. The receiver shader currently samples the physical page with the unmodified light-space page Y coordinate, so every physical page is read vertically mirrored.

The mismatch is mostly hidden when a caster is centered inside one physical page. When a caster crosses a virtual page boundary, the independently mirrored fragments no longer meet at the shared boundary and appear as separated or duplicated shadows.

## Evidence

The issue was isolated with `Physics Sphere 03` as the only caster:

- At its normal size, the sphere crossed four Level 0 virtual pages and produced two separated shadow fragments.
- Reducing the sphere so its projection stayed inside one page produced one continuous shadow.
- Changing the `126`-texel coordinate scaling did not change the split shadow.
- Restoring the original scaling and only sampling with `1.0 - pagePosition.y` produced one continuous circular shadow.
- Restoring all casters kept the circular and rectangular silhouettes continuous while the Editor remained near 72-75 FPS.

## Design

Keep the existing virtual page, physical atlas, gutter, page-table, cache, bias, and 3x3 PCF behavior unchanged.

In `SampleVirtualShadowPage`, convert the light-space page coordinate to physical texture orientation before calculating the atlas pixel:

```text
samplePagePosition.x = pagePosition.x
samplePagePosition.y = 1.0 - pagePosition.y
```

The existing saturation, content-range mapping, and PCF sampling then operate on `samplePagePosition`. This keeps the correction local to the point where virtual page coordinates become physical texture coordinates and avoids changing page projection winding or rasterizer culling.

## Alternatives Rejected

- Inverting the Y row of each page projection would also change triangle winding and interact with the shadow pass front-face culling.
- Using a negative-height viewport is not uniformly supported by the current D3D11, D3D12, and Metal RHI abstraction.
- Cross-page lookup per PCF tap is unnecessary for this orientation bug and would add shader cost.

## Verification

No engine-bound unit test is added. Verification uses Editor smoke tests launched with `--project`:

1. A temporary single-sphere scene confirms that a caster crossing four Level 0 pages produces one continuous shadow.
2. The scene is restored and both Scene View and Game View are inspected for continuous sphere and cube silhouettes.
3. Confirm that the Editor remains near its normal frame rate and that no shader or pipeline creation error appears in the log.
4. Restore all temporary scene and diagnostic artifacts before completion.
