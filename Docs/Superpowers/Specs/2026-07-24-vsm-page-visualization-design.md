# VSM Virtual Page Visualization Design

## Goal

Add a toggleable Scene View debug mode that colors opaque pixels by the virtual shadow page actually selected for sampling. The result should divide visible opaque geometry into stable, differently colored page-sized regions.

## Scope

- Add a `VSM Virtual Pages` checkbox to the Scene View `Render` popup.
- Apply the mode only to the Scene View opaque pass.
- Leave the Game View, transparent pass, normal material shading, and VSM page allocation unchanged.
- Keep the mode disabled by default.

## Data Flow

`SceneViewPanel` owns the checkbox state. The Editor captures the state with the other Scene View rendering options and passes it through `StandaloneRendererInitParam` to `OpaqueSceneRenderPassInitParam`.

The opaque pass writes the debug-mode flag into the existing unused padding field in `VirtualShadowGpuConstants`. This avoids adding a new uniform buffer or shader resource binding.

## Shader Behavior

When the mode is disabled, `BasicMesh.hlsl` keeps its current material, lighting, and shadow behavior.

When enabled, the pixel shader:

1. Selects the first VSM clipmap level using the existing camera-depth rule.
2. Computes the virtual page coordinate using the same biased receiver position used by shadow sampling.
3. Looks up page residency and follows the existing fallback order toward coarser clipmap levels.
4. Hashes the selected logical key `(clipmapLevel, pageX, pageY)` into a stable, bright RGB color.
5. Returns magenta when VSM is unavailable or no resident fallback page exists.

The color represents the logical virtual page actually used for sampling, not the physical atlas slot. Colors therefore remain spatially meaningful while still exposing missing pages and coarse fallback.

## Validation

- Confirm the checkbox defaults to disabled and affects only Scene View opaque rendering.
- Compile `BasicMesh.hlsl` through the Windows DXBC and DXIL paths and the Metal MSL path.
- Build the Windows Debug Editor target.
- Launch the Editor with `--project D:\github-desktop\VEngine\DemoProject` and confirm the enabled view shows stable colored regions with larger regions where coarse fallback is selected.

