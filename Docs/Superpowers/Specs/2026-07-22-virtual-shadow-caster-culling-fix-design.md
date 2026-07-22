# Virtual Shadow Caster Culling Fix Design

## Problem

The virtual shadow depth pass culls front-facing caster triangles. Closed meshes therefore write their back surfaces into the shadow atlas. A cube near the ground writes its bottom face and loses the whole contact shadow once bias exceeds the remaining gap. A sphere writes its lower hemisphere, so the center loses shadow first while the outer region remains as a ring.

## Change

Use the engine's existing `SolidBackCullRasterizer` state without overriding it to `Front`. This makes the shadow atlas retain the nearest light-facing caster surface. Do not change depth bias, normal bias, page allocation, clipmaps, or PCF in this experiment so the result isolates culling as the only variable.

## Verification

- Build `VEngineWinEditor`.
- Run the existing CTest suite.
- Launch `VEngineWinEditor.exe --project D:\github-desktop\VEngine\DemoProject`.
- In the physics scene, use Pause and Step to inspect a cube and sphere approaching the ground.
- Confirm the cube contact shadow remains present and the sphere shadow is filled rather than annular.

This renderer-bound regression is verified with the real editor scene rather than a new unit test, matching the repository testing policy and the user's earlier testing direction.
