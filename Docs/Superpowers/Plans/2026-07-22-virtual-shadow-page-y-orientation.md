# Virtual Shadow Physical Page Y Orientation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate split and duplicated shadows when a caster crosses virtual shadow page boundaries by reading each physical page with the same Y orientation used during rasterization.

**Architecture:** Keep the virtual page cache, page table, atlas layout, gutter, bias, and 3x3 PCF unchanged. Convert light-space page Y to downward-growing physical texture V locally inside `SampleVirtualShadowPage`, before the existing atlas-pixel calculation.

**Tech Stack:** C++20 engine, HLSL, D3D12 Editor smoke test, CMake/MSVC build scripts.

---

### Task 1: Correct physical-page Y sampling and verify cross-page continuity

**Files:**
- Modify: `Assets/Builtin/Shaders/BasicMesh.hlsl:113`
- Temporarily modify and restore for smoke isolation: `DemoProject/Assets/Scenes/SampleScene.vescene`
- Reference: `Docs/Superpowers/Specs/2026-07-22-virtual-shadow-page-y-orientation-design.md`

- [ ] **Step 1: Confirm the shader starts without diagnostic changes**

Run:

```powershell
git diff --exit-code -- Assets/Builtin/Shaders/BasicMesh.hlsl
```

Expected: exit code `0`. If the file contains unrelated user changes, preserve them and confirm that the temporary `samplePagePosition` diagnostic code is absent before proceeding.

- [ ] **Step 2: Apply the minimal Y-orientation correction**

Replace the current `centerPixel` calculation in `SampleVirtualShadowPage` with:

```hlsl
float2 samplePagePosition = float2(pagePosition.x, 1.0f - pagePosition.y);
float2 centerPixel = minimumPixel + saturate(samplePagePosition) * (contentSize - 1.0f);
```

Do not change `contentSize`, gutter bounds, PCF offsets, comparison sampling, page lookup, or bias handling.

- [ ] **Step 3: Build the Editor and shader tool**

Run:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineShaderTool VEngineWinEditor
```

Expected: both targets build successfully. The existing missing ATL/MFC library-path warning is acceptable; shader compilation or graphics-pipeline errors are not.

- [ ] **Step 4: Compile the modified built-in shader explicitly**

Run:

```powershell
Build/windows-msvc-debug/Debug/VEngineShaderTool.exe compile --source Assets/Builtin/Shaders/BasicMesh.hlsl --output Build/Generated/VirtualShadowPageYOrientation --name BasicMesh --dxc ThirdParty/DirectXShaderCompiler/Build/Windows64/1.9.2602.17/Tools/x64/dxc.exe --fxc fxc --slang ThirdParty/Slang/slang-2026.12-windows-x86_64/bin/slangc.exe
```

Expected: exit code `0`, with generated D3D11, D3D12, and Metal shader artifacts and no binding or syntax error.

- [ ] **Step 5: Repeat the controlled cross-page caster smoke test**

Using `apply_patch`, temporarily set `castShadows` to `false` for `Ground`, `Physics Cube 02` through `Physics Cube 10`, `Physics Sphere 01`, `Physics Sphere 02`, and `Physics Sphere 04` through `Physics Sphere 10`. Keep the existing `Physics Cube 01` value `false`, keep `Physics Sphere 03` `true`, and do not change the directional light.

Launch the Editor directly with the project path:

```powershell
Start-Process -FilePath Build/windows-msvc-debug/Debug/VEngineWinEditor.exe -ArgumentList '--project','D:\github-desktop\VEngine\DemoProject' -PassThru
```

Expected after project load:

- Scene View and Game View each show one continuous, approximately circular ground shadow from `Physics Sphere 03`.
- No second detached fragment is visible.
- The log contains no shader compilation, pipeline creation, or assertion failure.

Stop only the exact process started by this step. Use `apply_patch` to restore every temporary `castShadows` value, then parse the scene JSON and confirm `Physics Cube 01` is the only disabled mesh caster.

- [ ] **Step 6: Verify the complete scene and frame rate**

Launch again with the same `--project` command and inspect both views.

Expected:

- Sphere projections form continuous circular silhouettes.
- Cube projections form continuous rectangular silhouettes.
- No per-page mirrored fragments or duplicated shadows remain.
- Editor frame rate remains in the established approximate 72-75 FPS range after the project settles.

Stop only the exact process started by this step and remove screenshots or generated diagnostic images. Keep `Build/Generated/VirtualShadowPageYOrientation` as a disposable build artifact only; do not stage it.

- [ ] **Step 7: Check scope and repository cleanliness**

Run:

```powershell
git diff --check -- Assets/Builtin/Shaders/BasicMesh.hlsl DemoProject/Assets/Scenes/SampleScene.vescene
git diff -- DemoProject/Assets/Scenes/SampleScene.vescene
git diff -- Assets/Builtin/Shaders/BasicMesh.hlsl
```

Expected:

- `git diff --check` exits `0`.
- The scene diff contains only the pre-existing approved changes: corrected local ground bounds, the directional light pointing down world Y, and `shadowDistance` set to `512`.
- The shader diff contains only the two-line `samplePagePosition` correction and the updated `centerPixel` input.

- [ ] **Step 8: Commit the isolated shader fix**

Run:

```powershell
git add -- Assets/Builtin/Shaders/BasicMesh.hlsl
git commit -m "render: fix virtual shadow page y orientation"
```

Expected: the commit contains only `Assets/Builtin/Shaders/BasicMesh.hlsl`; existing unrelated working-tree changes remain unstaged.
