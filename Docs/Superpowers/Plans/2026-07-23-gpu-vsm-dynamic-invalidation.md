# GPU VSM Dynamic Caster Invalidation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve GPU VSM page mappings during Play and redraw only resident pages overlapped by moving, added, removed, or shadow-state-changing casters.

**Architecture:** Reuse `VirtualShadowInvalidationTracker` to generate compact absolute page keys from old and new caster bounds. Upload those keys through the existing VSM constant-buffer page-entry storage, mark matching persistent physical pages dirty in the request-clear compute pass, and reserve full mapping resets for incompatible light/resource changes.

**Tech Stack:** C++20, HLSL Shader Model 5/6, VEngine VirtualShadow cache, FrameGraph, D3D11, D3D12, CTest.

---

### Task 1: Specify GPU Packet Invalidation Behavior With A Failing Test

**Files:**
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`

- [x] Add a test-only-usable public overload `PrepareFrame(const VirtualShadowPrepareInput& input)`.
- [x] Add `TestGpuViewCacheLocalInvalidation` that prepares one caster, consumes the initial resource reset, moves the caster by changing its revision and bounds, and requires:
  - the second packet remains GPU driven;
  - the second packet does not request a full mapping reset;
  - invalidated keys cover both old and new caster bounds;
  - a camera cut with unchanged casters preserves mappings;
  - a light-direction change requests a full mapping reset.
- [x] Register the test in `main`.
- [x] Build and run `VEngineVirtualShadowTests`; confirm the test fails because the overload and packet invalidation fields do not exist.

### Task 2: Produce Local Invalidation Keys In GPU Frame Preparation

**Files:**
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`

- [x] Add `invalidatedPageKeys` and `invalidateAllGpuPages` to `VirtualShadowFramePacket`.
- [x] Implement `PrepareFrame(const VirtualShadowPrepareInput&)`.
- [x] Build opaque shadow-caster snapshots and call the existing per-view `VirtualShadowInvalidationTracker`.
- [x] Set `resetGpuCache` only for light-direction full invalidation, shadow-distance changes, or pending GPU-resource reset.
- [x] Preserve compatible mappings across camera cuts and ordinary caster revisions.
- [x] Set `invalidateAllGpuPages` when the compact key list exceeds `VirtualShadowMaxInvalidationPageCount`.
- [x] Refactor the RTScene overload to assemble `VirtualShadowPrepareInput` and call the new overload.
- [x] Run `VEngineVirtualShadowTests`; confirm packet behavior passes.

### Task 3: Upload Invalidation Keys Through Existing Constants

**Files:**
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- Modify: `Assets/Builtin/Shaders/BasicMesh.hlsl`

- [x] Define `InvalidVirtualShadowGpuInvalidationCount` as the full-content invalidation sentinel.
- [x] Use `VirtualShadowGpuConstants::padding[1]` as `invalidationCount`; keep `padding[0]` as the per-pass clipmap level.
- [x] In GPU mode, copy invalidated absolute keys into the existing `entries` array and set the sentinel for overflow.
- [x] Rename the matching two HLSL padding scalars for layout clarity without changing constant-buffer offsets.
- [x] Add unit assertions for encoded count, key order/content, and full-content sentinel.
- [x] Run the test first to observe the missing encoding failure, implement encoding, then confirm it passes.

### Task 4: Dirty Matching Persistent Physical Pages On GPU

**Files:**
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.cpp`

- [x] Extend `CommonConstants` with the existing 2,048-entry key array at the exact C++ offset.
- [x] In `ClearComputeHlsl`, preserve physical keys and age when `resetCache == 0`.
- [x] Mark a valid physical page dirty when its absolute key matches any uploaded invalidation key.
- [x] For the sentinel, mark every valid physical page dirty while preserving its mapping.
- [x] Clear only the current-frame requested bit; allow dirty unrequested pages to persist.
- [x] Add and run a source-contract test that fails before the HLSL change and passes afterward.

### Task 5: Verify Dynamic Performance And Correctness

**Files:**
- Modify: `Docs/DevelopmentPlan.md`
- Modify: `Docs/Superpowers/Plans/2026-07-23-gpu-vsm-dynamic-invalidation.md`

- [x] Build and run all Windows CTest targets.
- [x] Build Editor and Player.
- [x] Compile `BasicMesh.hlsl` to DXBC, DXIL, and MSL.
- [x] Smoke-test DemoProject on D3D11 and D3D12 and inspect logs for shader errors or unexpected VSM disablement.
- [x] Measure the maximized 2576-by-1416 dual-view D3D12 Editor both while static and during Play.
- [x] Verify moving objects update shadows without whole-pool reset artifacts.
- [x] Run `git diff --check`.
- [x] Request final code review and resolve all Critical or Important findings.

Observed result: 75.0 FPS both static and during Play in the maximized 2576-by-1416 D3D12 dual-view scene. The previous moving-object regression to about 25 FPS no longer reproduces.

Final-review hardening:

- [x] Generate GPU invalidation identities outside the current clipmap working region and match resident pages by absolute XY plus clipmap level, preventing stale dormant pages after camera pan-away/pan-back.
- [x] Commit shadow-distance and caster history only after the frame is eligible to submit the GPU VSM passes.
- [x] Add regression coverage for dormant resident pages, disabled-light shadow-distance changes, and invalid-projection recovery.
- [x] Re-run final review after the hardening changes.

Commands:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
Build/windows-msvc-debug/Debug/VEngineShaderTool.exe compile --source Assets/Builtin/Shaders/BasicMesh.hlsl --output Build/windows-msvc-debug/ShaderValidation/GpuVsmDynamicInvalidation --name BasicMesh --dxc ThirdParty/DirectXShaderCompiler/Build/Windows64/1.9.2602.17/Tools/x64/dxc.exe --fxc "C:/Program Files (x86)/Windows Kits/10/bin/10.0.22621.0/x64/fxc.exe" --slang ThirdParty/Slang/slang-2026.12-windows-x86_64/bin/slangc.exe
git diff --check
```
