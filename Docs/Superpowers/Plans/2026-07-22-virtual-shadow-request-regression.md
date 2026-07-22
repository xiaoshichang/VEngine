# Virtual Shadow Request Regression Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct the DemoProject Ground's double-scaled render bounds and measure its effect on VSM request count and frame time.

**Architecture:** Keep the VSM implementation unchanged for the first experiment. Correct the local content bounds, run the same instrumented Editor workload, compare with the captured baseline, then remove temporary diagnostics.

**Tech Stack:** VEngine scene JSON, C++20 diagnostics, Windows Editor, D3D12.

---

### Task 1: Correct Ground Local Bounds

**Files:**
- Modify: `DemoProject/Assets/Scenes/SampleScene.vescene`

- [ ] Change the Ground `MeshRenderComponent.boundsExtents` from `[8, 0.25, 5]` to `[0.5, 0.5, 0.5]` while preserving its Transform scale.
- [ ] Verify the scene JSON still parses and no unrelated scene values changed.

### Task 2: Measure the Corrected Scene

**Files:**
- Read: `Build/windows-msvc-debug/Debug/Logs/VEngine.log`

- [ ] Launch `VEngineWinEditor`, open the existing DemoProject recent-project entry, and render for at least 20 seconds.
- [ ] Compare requested, missing, request-build, request-resolve, preparation, and 30-frame cadence values against the recorded baseline.
- [ ] Decide from evidence whether content bounds alone recover acceptable performance or whether the page-cache algorithm remains a separate required fix.

### Task 3: Remove Temporary Diagnostics and Verify

**Files:**
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`

- [ ] Remove only the temporary `VirtualShadow` timing/statistics logs and their diagnostic-only chrono/log includes.
- [ ] Build `VEngineWinEditor` and `VEngineWinPlayer`.
- [ ] Build and run the complete Windows CTest preset.
- [ ] Run `git diff --check` and inspect the final worktree without committing or creating a worktree.
