# Virtual Shadow Caster Culling Fix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve filled contact shadows for cubes and spheres by writing the nearest light-facing caster surfaces into virtual shadow pages.

**Architecture:** Keep the existing virtual shadow pipeline, atlas, page cache, biases, and sampling unchanged. Remove the single front-cull override so the already-selected back-cull rasterizer state controls caster rendering.

**Tech Stack:** C++20, VEngine RHI, D3D12, CMake/CTest, Dear ImGui editor smoke test

---

### Task 1: Restore back-face culling in the virtual shadow depth pass

**Files:**
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.cpp:234`

- [ ] **Step 1: Record the failing visual regression**

Use the supplied screenshots as the RED evidence: the cube shadow disappears near ground contact and the sphere shadow becomes an annulus. Do not add an engine-bound unit test.

- [ ] **Step 2: Apply the single-variable fix**

Keep:

```cpp
pipelineDesc.rasterizerState = rhi::StaticRenderStates::SolidBackCullRasterizer;
```

Remove:

```cpp
pipelineDesc.rasterizerState.cullMode = rhi::RhiCullMode::Front;
```

- [ ] **Step 3: Build the editor**

Run:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: `VEngineWinEditor.exe` links successfully.

- [ ] **Step 4: Run the existing test suite**

Run:

```powershell
[System.Environment]::SetEnvironmentVariable('PATH', $null, [System.EnvironmentVariableTarget]::Process)
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: all registered tests pass.

- [ ] **Step 5: Run the real editor regression scene**

Launch:

```powershell
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project D:\github-desktop\VEngine\DemoProject
```

Use Play, Pause, and Step in the physics scene. Expected: cube shadows remain until contact and sphere shadows remain filled without a hollow ring.

- [ ] **Step 6: Commit the confirmed fix**

```powershell
git add Docs/Superpowers/Specs/2026-07-22-virtual-shadow-caster-culling-fix-design.md Docs/Superpowers/Plans/2026-07-22-virtual-shadow-caster-culling-fix.md Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.cpp
git commit -m "render: fix virtual shadow caster culling"
```
