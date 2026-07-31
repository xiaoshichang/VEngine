# VSM Stable Statistics, Global Pool, And Clipmap Tuning Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct stable-frame VSM counters, reuse one manager-owned GPU resource pool across `RTScene` replacement, and tune clipmap coverage until the demo scene requests approximately 100 pages.

**Architecture:** `VirtualShadowManager::Impl` owns the only `VirtualShadowSceneCache`, while each `SceneEntry` owns only invalidation/reset/statistics state. A small FrameGraph compute pass rewrites persistent statistics to stable-frame reuse semantics before readback. A named clipmap coverage scale increases all level radii and derived page world sizes without changing the atlas, physical-page size, or clipmap count.

**Tech Stack:** C++20, VEngine FrameGraph and common RHI, embedded HLSL, D3D11/D3D12, CMake/MSVC, CTest, Editor D3D11 smoke diagnostics.

---

## File Structure

### Modified production files

- `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h`
  - Remove scene-local invalidation ownership and expose an explicit logical reset request for the manager-owned pool.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
  - Implement the reset request and remove invalidation accessors.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
  - Own one global cache, move invalidation tracking into `SceneEntry`, reset the pool on scene transition, and schedule stable statistics.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.h`
  - Declare the stable statistics FrameGraph pass and recorder.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.cpp`
  - Add the one-thread stable statistics compute pipeline, dependency declaration, and recording implementation.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h`
  - Define the named coverage scale used by all clipmap levels.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.cpp`
  - Apply the coverage scale before deriving page world size.

### Temporary verification files

- `Tests/Unit/VirtualShadowChangeDiagnostics.cpp`
- `CMake/Targets/Tests/VirtualShadowChangeDiagnostics.cmake`
- `CMake/Targets/Tests.cmake`
  - Used only for red/green diagnostics, then removed before the final diff so the repository does not retain new VSM unit tests.

## Task 1: Establish Red Diagnostics

**Files:**

- Create temporarily: `Tests/Unit/VirtualShadowChangeDiagnostics.cpp`
- Create temporarily: `CMake/Targets/Tests/VirtualShadowChangeDiagnostics.cmake`
- Modify temporarily: `CMake/Targets/Tests.cmake`

- [ ] **Step 1: Add a focused executable that captures the missing public behavior**

Create a temporary diagnostic with:

```cpp
#include "Engine/Runtime/Math/Math.h"
#include "Engine/Runtime/Math/Matrix44.h"
#include "Engine/Runtime/Math/Vector3.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h"
#include "Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h"

#include <iostream>

int main()
{
    bool passed = true;
    const ve::VirtualShadowClipmapSet clipmaps =
        ve::BuildVirtualShadowClipmaps(ve::Matrix44::Identity(), ve::Vector3(0.0f, -1.0f, 1.0f), 256.0f);
    const float expectedRadius = (256.0f / 8.0f) * 2.5f;
    if (!clipmaps.valid || !ve::NearlyEqual(clipmaps.levels[0].worldRadius, expectedRadius))
    {
        std::cerr << "FAILED: clipmap coverage scale was not applied\n";
        passed = false;
    }

    ve::VirtualShadowSceneCache cache;
    cache.RequestSceneCacheReset();
    if (!cache.IsSceneCacheResetPending() || !cache.ConsumeSceneCacheReset() || cache.IsSceneCacheResetPending())
    {
        std::cerr << "FAILED: manager-owned pool reset request is not consumable exactly once\n";
        passed = false;
    }
    return passed ? 0 : 1;
}
```

Register it as `VEngineVirtualShadowChangeDiagnostics` following the existing unit target pattern.

- [ ] **Step 2: Configure and build to verify RED**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineVirtualShadowChangeDiagnostics
```

Expected: compilation fails because `RequestSceneCacheReset()` does not exist. This establishes RED before either production behavior is changed.

## Task 2: Move GPU Resources To The Manager-Wide Pool

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`

- [ ] **Step 1: Make pool reset explicit and remove scene tracking from the pool**

In `VirtualShadowSceneCache`, add:

```cpp
void RequestSceneCacheReset() noexcept;
```

Implement it as:

```cpp
void VirtualShadowSceneCache::RequestSceneCacheReset() noexcept
{
    resetSceneCachePending_ = true;
}
```

Remove `VirtualShadowInvalidationTracker` includes, members, and accessors from the cache. Do not change GPU resource creation or release.

- [ ] **Step 2: Give `VirtualShadowManager::Impl` one cache**

Change `SceneEntry` to:

```cpp
struct SceneEntry
{
    std::weak_ptr<RTScene> scene;
    VirtualShadowInvalidationTracker invalidationTracker;
    virtual_shadow_detail::SceneState state;
    UInt64 statisticsIdentity = 0;
};
```

Change manager storage to:

```cpp
explicit Impl(VirtualShadowSceneCacheDesc desc)
    : sceneCache(std::move(desc))
{
}

VirtualShadowSceneCache sceneCache;
RTScene* activePoolScene = nullptr;
```

`GetOrCreateSceneEntry()` must no longer allocate a cache.

- [ ] **Step 3: Reset logical contents, not GPU objects, on scene transition**

After acquiring the current `SceneEntry`:

```cpp
VirtualShadowSceneCache& sceneCache = impl_->sceneCache;
sceneCache.EnsureResources(*frameData.device);
if (impl_->activePoolScene != rendererData.scene.get())
{
    impl_->activePoolScene = rendererData.scene.get();
    sceneCache.RequestSceneCacheReset();
}
```

Use `sceneEntry.invalidationTracker.UpdateScene(...)`. Make view registration, slice release/acquire, expired-view cleanup, FrameGraph imports, sampling snapshots, and capacity reporting all use `impl_->sceneCache`.

When `CollectExpiredState()` removes the active scene entry, clear `activePoolScene` but retain the cache resources.

- [ ] **Step 4: Build and inspect the intermediate diagnostic**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineVirtualShadowChangeDiagnostics
Build/windows-msvc-debug/Debug/VEngineVirtualShadowChangeDiagnostics.exe
```

Expected: the reset diagnostic passes; the clipmap assertion may still fail until Task 4.

## Task 3: Generate Stable-Frame Reuse Statistics

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`

- [ ] **Step 1: Add the stable statistics shader and pipeline**

Add a one-UAV compute shader:

```hlsl
RWStructuredBuffer<uint> Statistics : register(u0);
[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint requestedPages = Statistics[1];
    Statistics[2] = requestedPages;
    Statistics[3] = 0u;
    Statistics[4] = 0u;
    Statistics[5] = 0u;
}
```

Create a dedicated compute pipeline layout with only `ReadWriteStorageBuffer(u0)`. Include this pipeline in `ValidateVirtualShadowPagePipelines()` so missing mandatory shader state remains fail-fast.

- [ ] **Step 2: Add recording and FrameGraph registration**

Declare:

```cpp
void AddVirtualShadowReuseStatisticsPass(FrameGraph& frameGraph, VirtualShadowFrameGraphResources& resources);
```

The pass must:

- validate `resources.statistics`;
- declare it with `builder.Write(resources.statistics)`;
- update the shared handle version;
- record one compute dispatch;
- execute before `AddVirtualShadowStatisticsReadbackPass()`.

- [ ] **Step 3: Schedule exactly one statistics path**

In `VirtualShadowManager::AddToFrameGraph()`:

```cpp
if (familyRequiresUpdate)
{
    AddVirtualShadowRequestAndResidencyPasses(frameGraph, graphData.virtualShadowPreparedFamily, graphData.virtualShadowResources);
    for (const VirtualShadowPreparedViewGraphData& preparedView : graphData.virtualShadowPreparedFamily.views)
    {
        if (!preparedView.packet.requiresPageRendering)
        {
            continue;
        }
        AddVirtualShadowClearPhysicalPagesPass(frameGraph, preparedView, graphData.virtualShadowResources);
        AddVirtualShadowRenderCastersPass(frameGraph, preparedView, graphData.virtualShadowResources);
        AddVirtualShadowMarkRenderedPass(frameGraph, preparedView, graphData.virtualShadowResources);
    }
    AddVirtualShadowFinalizeScenePass(frameGraph, graphData.virtualShadowPreparedFamily, graphData.virtualShadowResources);
}
else
{
    AddVirtualShadowReuseStatisticsPass(frameGraph, graphData.virtualShadowResources);
}
AddVirtualShadowStatisticsReadbackPass(frameGraph, graphData.virtualShadowResources);
```

Do not regenerate requests or scan physical pages in the stable branch.

- [ ] **Step 4: Build the Editor**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: compilation and link succeed.

## Task 4: Enlarge Clipmap Coverage

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.cpp`

- [ ] **Step 1: Add and apply the initial scale**

Add:

```cpp
inline constexpr Float32 VirtualShadowClipmapCoverageScale = 2.5f;
```

Calculate each level as:

```cpp
level.worldRadius =
    shadowDistance * VirtualShadowClipmapCoverageScale /
    static_cast<Float32>(1u << (VirtualShadowClipmapLevelCount - 1u - levelIndex));
level.pageWorldSize = (2.0f * level.worldRadius) / static_cast<Float32>(VirtualShadowPagesPerAxis);
```

- [ ] **Step 2: Run the temporary diagnostic to verify GREEN**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineVirtualShadowChangeDiagnostics
Build/windows-msvc-debug/Debug/VEngineVirtualShadowChangeDiagnostics.exe
```

Expected: exit code 0.

- [ ] **Step 3: Tune using the demo scene**

Launch:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Observe the stationary warmed request count. If it is outside 80 to 120, adjust only `VirtualShadowClipmapCoverageScale`, rebuild, and repeat. Preserve four levels, atlas extent, physical page size, and the 3-by-3 request guard band.

## Task 5: Remove Temporary Tests And Verify The Integrated Result

**Files:**

- Delete: `Tests/Unit/VirtualShadowChangeDiagnostics.cpp`
- Delete: `CMake/Targets/Tests/VirtualShadowChangeDiagnostics.cmake`
- Restore: `CMake/Targets/Tests.cmake`

- [ ] **Step 1: Remove temporary diagnostic registration and files**

Delete the temporary target and restore `Tests.cmake` to the five existing registered unit-test groups.

- [ ] **Step 2: Check the final diff**

Run:

```text
git diff --check
git status --short
```

Expected: no whitespace errors and only the seven intended production files plus this plan are changed.

- [ ] **Step 3: Run the complete Windows verification**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: build exit code 0 and all registered tests pass.

- [ ] **Step 4: Run Editor smoke verification**

Launch the Editor with the demo project and verify:

- after warm-up, stationary statistics show `cachedPages == requestedPages`;
- after warm-up, `redrawnPages == 0`;
- stationary `requestedPages` is between 80 and 120;
- entering Play does not create a second VSM atlas or supporting buffer bundle;
- the first Play frame still performs a correct logical reset and then returns to stable reuse.

- [ ] **Step 5: Commit the production change**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.h Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.cpp Docs/Superpowers/Plans/2026-07-29-vsm-stable-statistics-global-pool-clipmap-tuning.md
git commit -m "render: reuse VSM pool and correct stable statistics"
```
