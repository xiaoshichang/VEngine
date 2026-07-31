# GPU VSM Request Compaction Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove redundant per-pixel VSM page atomics and the allocator's full logical-page scan while preserving conservative 3-by-3 coverage and coarse-level fallback.

**Architecture:** The depth-pixel pass marks only center pages from the selected clipmap level through the coarsest fallback level. A logical-page compute pass expands the center marks by one page, compacts unique requests into four level-partitioned lists, and the existing correctness-first allocator consumes those lists coarse-first.

**Tech Stack:** C++20, HLSL Shader Model 5/6, VEngine FrameGraph, D3D11, D3D12.

---

### Task 1: Define Camera And Request-List Data

**Files:**
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`

- [x] Add camera world position and forward vectors to `VirtualShadowGpuConstants`.
- [x] Populate them from the render camera in GPU mode and preserve matching layout in `BasicMesh.hlsl`.
- [x] Add persistent `requestList` storage for 65,536 logical indices and four per-level request counters.
- [x] Import the two buffers only for GPU-driven VSM.

### Task 2: Verify The Old Per-Pixel Expansion Fails The New Contract

**Files:**
- Inspect: `Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.cpp`

- [x] Run a source-contract check that requires a compact pass, level counters, and no neighbor loop in `MarkComputeHlsl`.
- [x] Confirm it fails before production changes because the current marker performs 3-by-3 expansion per pixel.

### Task 3: Implement Center Marking And Request Compaction

**Files:**
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.cpp`

- [x] Compute camera depth and select `firstLevel` with the same radius rule as `BasicMesh.hlsl`.
- [x] Mark only the center page from `firstLevel` through `clipmapCount`.
- [x] Clear the four request counters with the existing reset pass.
- [x] Add `VirtualShadowCompactRequests`: one thread per logical page reads the 3-by-3 center-mark neighborhood and appends the logical address into its level's reserved 16,384-entry list segment.
- [x] Declare explicit FrameGraph reads/writes for marks, request list, and counters.

### Task 4: Allocate From Compact Lists

**Files:**
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.cpp`

- [x] Replace the allocator's 65,536-address loop with four coarse-to-fine request-list loops.
- [x] Keep the existing physical cache hit, free-page, pinning, and LRU behavior unchanged.
- [x] Bind request list and request counters as read-only compute resources.

### Task 5: Verify

**Files:**
- Modify: `Docs/DevelopmentPlan.md`

- [x] Run the source-contract check and confirm it passes.
- [x] Build and run all Windows CTest targets.
- [x] Compile `BasicMesh.hlsl` to DXBC, DXIL, and MSL.
- [x] Build Editor and Player.
- [x] Smoke-test DemoProject on D3D11 and D3D12 and inspect logs for fallback or shader errors.
- [x] Run `git diff --check`.

Additional profiling fixes completed during verification:

- Interleave parallel resident-page hit resolution and serial miss allocation once per clipmap level in coarse-to-fine order. A finer cached page therefore cannot pin physical capacity before all coarser requests have been serviced.
- Stop miss processing once every physical page is pinned for the current frame.
- Consume the per-view GPU cache reset request unconditionally before merging it with resource-recreation state. The previous short-circuit expression left the reset request pending forever and rebuilt every physical page every frame.
- Increase the Scene View atlas from 2,048 to 2,560 pixels, raising its physical pool from 256 to 400 pages. The original pool could not hold the conservative coarse-level request set at the maximized viewport size.
- Under the same maximized 2576-by-1416 dual-view D3D12 Editor workload, the observed frame rate increased from about 20–25 FPS to 74.9 FPS while retaining complete shadows and coarse-level priority.

Commands:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
Build/windows-msvc-debug/Debug/VEngineShaderTool.exe compile --source Assets/Builtin/Shaders/BasicMesh.hlsl --output Build/windows-msvc-debug/ShaderValidation/GpuVsmRequestCompaction --name BasicMesh --dxc ThirdParty/DirectXShaderCompiler/Build/Windows64/1.9.2602.17/Tools/x64/dxc.exe --fxc fxc --slang ThirdParty/Slang/slang-2026.12-windows-x86_64/bin/slangc.exe
git diff --check
```
