# VSM View Family And Shared Page Pool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prepare directional-light VSM work once per scene view family and let every single-view renderer consume a scene-shared physical page pool through an isolated page-table slice.

**Architecture:** `RenderSystemImpl` owns one `VirtualShadowManager`; the manager keeps one `VirtualShadowSceneCache` per live `RTScene` and invokes a direct-RHI `VirtualShadowGpuPipeline` before any renderer in a family. `RenderViewState` keeps only stable view identity and projection state, while `BaseRenderer` remains single-view and receives an immutable `VirtualShadowViewResult`.

**Tech Stack:** C++20, VEngine common RHI, GPU compute/raster HLSL embedded in C++, CMake, CTest, D3D11, D3D12.

---

## File Structure

### New files

- `Engine/Runtime/Render/RenderViewFamily.h`
  - Defines `RendererRenderTarget`, `RenderView`, and `RenderViewFamily`.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h`
  - Defines scene-cache configuration, page-table slices, the slice allocator, and shared resource accessors.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
  - Owns the shared atlas, fallback bindings, page-table/request buffers, physical metadata, and slice lifecycle.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.h`
  - Declares the direct-RHI GPU preparation stages.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
  - Hosts the migrated VSM shaders, pipeline creation, compute dispatches, and receiver/page raster recording.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
  - Declares the render-system-owned family preparation API and immutable per-view result.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
  - Owns view IDs, scene-cache lookup, invalidation orchestration, family request order, and expired-state cleanup.

### Modified files

- `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
  - Replaces depth epoch in the packed key with the 24-bit stable view ID.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h/.cpp`
  - Tracks scene caster changes once and returns changed bounds instead of per-view page keys.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h/.cpp`
  - Becomes CPU-only view projection/invalidation preparation state.
- `Engine/Runtime/Render/RenderViewState.h/.cpp`
  - Stores stable view ID and removes per-view GPU pool ownership.
- `Engine/Runtime/Render/RenderFramePipelineData.h`
  - Exposes the render-system manager to frame pipelines.
- `Engine/Runtime/Render/RenderFramePipeline.h/.cpp`
  - Builds families, runs `PreRenderShadowStep`, then creates one renderer per view.
- `Engine/Runtime/Render/RenderSystem.cpp`
  - Owns and publishes the manager.
- `Engine/Runtime/Render/Renderer/BaseRenderer.h/.cpp`
  - Accepts a `RenderView` and consumes manager-prepared resources.
- `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h`
  - Removes allocation-only graph resources and adds the page-table buffer range.
- `Engine/Runtime/Render/Renderer/StandaloneRenderer.h/.cpp`
  - Removes `GpuVirtualShadowRenderPass`.
- `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
  - Binds the current view's page-table slice and manager-owned sampler.
- `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
  - Binds the same immutable view result.
- `Editor/Core/Editor.cpp`
  - Populates the new nested `RenderView`.
- `Engine/Runtime/Scene/SceneSystem.cpp`
  - Populates a one-view Player family input.
- `Tests/Unit/VirtualShadowTests.cpp`
  - Covers the new key, origin reuse, projection invalidation, view IDs, slices, and deterministic allocation helpers.
- `CMake/Targets/Engine.cmake`
  - Registers new files and removes the obsolete render-pass files.
- `Docs/ArchitectureOverview.md`
  - Records view-family preparation and per-scene VSM ownership.

### Removed files

- `Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.h`
- `Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.cpp`

Their shader and command-recording behavior moves to `VirtualShadowGpuPipeline`; no compatibility wrapper remains.

## Task 1: Change Physical Page Identity To Stable View ID

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.cpp`
- Test: `Tests/Unit/VirtualShadowTests.cpp`

- [ ] **Step 1: Rewrite the page-key test first**

Replace the depth-epoch assertions in `TestPageKeys` with:

```cpp
bool TestPageKeys()
{
    constexpr ve::UInt32 ViewID = 17;
    const ve::VirtualShadowPageKey key = ve::VirtualShadowPageKey::Create(-123, 456, 2, ViewID);

    bool passed = true;
    passed &= Expect(key.IsValid(), "Representable absolute VSM coordinates should produce a valid key");
    passed &= Expect(key.GetPageX() == -123 && key.GetPageY() == 456, "VSM key should preserve signed absolute page coordinates");
    passed &= Expect(key.GetClipmapLevel() == 2, "VSM key should preserve the clipmap level");
    passed &= Expect(key.GetViewID() == ViewID, "VSM key should preserve the stable view ID");
    passed &= Expect(!ve::VirtualShadowPageKey::Create(32768, 0, 0, ViewID).IsValid(), "Out-of-range X should be rejected");
    passed &= Expect(!ve::VirtualShadowPageKey::Create(0, 0, ve::VirtualShadowClipmapLevelCount, ViewID).IsValid(),
                     "Out-of-range clipmap level should be rejected");
    passed &= Expect(!ve::VirtualShadowPageKey::Create(0, 0, 0, 0).IsValid(), "View ID zero should remain invalid");
    passed &= Expect(!ve::VirtualShadowPageKey::Create(0, 0, 0, 0x01000000u).IsValid(), "View IDs wider than 24 bits should be rejected");
    return passed;
}
```

Update invalidation tests to call `BuildVirtualShadowPageKeysForBounds(ViewID, clipmaps, bounds)` and compare the full key including `GetViewID()`.

- [ ] **Step 2: Run the focused test and verify it fails**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests
```

Expected: compilation fails because `GetViewID()` and the view-first bounds-key overload do not exist.

- [ ] **Step 3: Implement the packed key**

Use this contract in `VirtualShadowTypes.h`:

```cpp
constexpr UInt32 InvalidVirtualShadowViewID = 0;
constexpr UInt32 VirtualShadowMaximumViewID = 0x00FFFFFFu;

struct VirtualShadowPageKey
{
    UInt32 key0 = 0xFFFFFFFFu;
    UInt32 key1 = 0xFFFFFFFFu;

    [[nodiscard]] static VirtualShadowPageKey Create(Int32 pageX, Int32 pageY, UInt32 clipmapLevel, UInt32 viewID) noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] Int32 GetPageX() const noexcept;
    [[nodiscard]] Int32 GetPageY() const noexcept;
    [[nodiscard]] UInt32 GetClipmapLevel() const noexcept;
    [[nodiscard]] UInt32 GetViewID() const noexcept;
};
```

Implement `Create` so `key0` keeps the signed 16-bit X/Y representation and:

```cpp
result.key1 = clipmapLevel | (viewID << 8u);
```

Delete `GetDepthEpoch`, `VirtualShadowMinimumDepthEpoch`, `VirtualShadowMaximumDepthEpoch`, and `IsVirtualShadowDepthEpochRepresentable`. Keep `VirtualShadowClipmapLevel::depthEpoch`; it remains projection compatibility data.

Change bounds-key construction to:

```cpp
[[nodiscard]] std::vector<VirtualShadowPageKey>
BuildVirtualShadowPageKeysForBounds(UInt32 viewID, const VirtualShadowClipmapSet& clipmaps, const Aabb& worldBounds);
```

Pass `viewID` to every `VirtualShadowPageKey::Create` call. GPU invalidation comparisons now use the complete `key0/key1`; no comparison may mask away the high 24 bits.

- [ ] **Step 4: Run the focused test**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineVirtualShadowTests --output-on-failure
```

Expected: build and test pass.

- [ ] **Step 5: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.cpp Tests/Unit/VirtualShadowTests.cpp
git commit -m "render: key VSM pages by stable view ID"
```

## Task 2: Introduce Render Views, Families, And Stable View State

**Files:**

- Create: `Engine/Runtime/Render/RenderViewFamily.h`
- Modify: `Engine/Runtime/Render/RenderViewState.h`
- Modify: `Engine/Runtime/Render/RenderViewState.cpp`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RendererFactory.cpp`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/MobileRenderer.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- Modify: `Editor/Core/Editor.cpp`
- Modify: `Engine/Runtime/Scene/SceneSystem.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Test: `Tests/Unit/VirtualShadowTests.cpp`

- [ ] **Step 1: Add failing stable-state tests**

Add:

```cpp
bool TestRenderViewStateIdentity()
{
    auto state = std::make_shared<ve::RTRenderViewState>(ve::RenderViewStateDesc{"TestView"});

    bool passed = true;
    passed &= Expect(state->GetVirtualShadowViewID() == ve::InvalidVirtualShadowViewID, "A new view should wait for manager registration");
    passed &= Expect(state->TryAssignVirtualShadowViewID(41), "The first valid view-ID assignment should succeed");
    passed &= Expect(state->GetVirtualShadowViewID() == 41, "Assigned view ID should persist");
    passed &= Expect(state->TryAssignVirtualShadowViewID(41), "Repeating the same assignment should be harmless");
    passed &= Expect(!state->TryAssignVirtualShadowViewID(42), "A stable view ID must not be replaced");
    return passed;
}
```

- [ ] **Step 2: Run the test and verify the missing API**

Run the `VEngineVirtualShadowTests` build command from Task 1.

Expected: compilation fails on `GetVirtualShadowViewID` and `TryAssignVirtualShadowViewID`.

- [ ] **Step 3: Define the lightweight family model**

Move `RendererRenderTarget` out of `BaseRenderer.h` and create:

```cpp
#pragma once

#include "Engine/RHI/Common/RhiTypes.h"
#include "Engine/Runtime/Render/RenderTexture.h"

#include <memory>
#include <vector>

namespace ve
{
    class RTCamera;
    class RTRenderViewState;
    class RTScene;
    struct VirtualShadowViewResult;

    struct RendererRenderTarget
    {
        std::shared_ptr<RTRenderTexture> colorTexture;
        rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
    };

    struct RenderView
    {
        std::shared_ptr<RTCamera> camera;
        std::shared_ptr<RTRenderViewState> viewState;
        RendererRenderTarget target;
        rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
        std::shared_ptr<VirtualShadowViewResult> virtualShadowResult;
    };

    struct RenderViewFamily
    {
        std::shared_ptr<RTScene> scene;
        std::vector<RenderView> views;
    };
} // namespace ve
```

Change `BaseRendererInitParam` to:

```cpp
struct BaseRendererInitParam
{
    const FrameRenderPipelineData* frameData = nullptr;
    std::shared_ptr<RTScene> scene;
    RenderView view;
};
```

Update Editor and Player construction sites from `rendererInitParam.camera`, `viewState`, `target`, and `fillMode` to the matching `rendererInitParam.view.*` fields.

- [ ] **Step 4: Make RenderViewState CPU-only and identity-bearing**

Change `RenderViewStateDesc` to contain only the name. Keep `VirtualShadowViewCache` temporarily, but give it a default constructor that uses the current 4096 atlas extent until Task 8 removes its transitional GPU resources:

```cpp
struct RenderViewStateDesc
{
    std::string name = "RenderView";
};
```

Add:

```cpp
[[nodiscard]] UInt32 GetVirtualShadowViewID() const noexcept;
[[nodiscard]] bool TryAssignVirtualShadowViewID(UInt32 viewID) noexcept;

UInt32 virtualShadowViewID_ = InvalidVirtualShadowViewID;
```

`TryAssignVirtualShadowViewID` accepts a representable nonzero ID only when the stored value is invalid or equal to the input. Update the Player state construction to `RenderViewStateDesc{"PlayerView"}` and update Editor state construction the same way.

Update `BaseRenderer.cpp` to read `initParam.view.camera`, `viewState`, and `target`. Update `StandaloneRenderer.cpp` and `MobileRenderer.cpp` to read `initParam.view.fillMode` and `initParam.view.target.colorLoadAction`. Update `RendererFactory.cpp` so derived renderer parameters preserve the nested `RenderView`.

- [ ] **Step 5: Register files, build, and test**

Add `RenderViewFamily.h` to the public `VEngine` sources. Run the focused build and CTest command.

Expected: `VEngineVirtualShadowTests` passes.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/RenderViewFamily.h Engine/Runtime/Render/RenderViewState.h Engine/Runtime/Render/RenderViewState.cpp Engine/Runtime/Render/Renderer/BaseRenderer.h Engine/Runtime/Render/Renderer/BaseRenderer.cpp Engine/Runtime/Render/Renderer/RendererFactory.cpp Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp Engine/Runtime/Render/Renderer/MobileRenderer.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp Editor/Core/Editor.cpp Engine/Runtime/Scene/SceneSystem.cpp CMake/Targets/Engine.cmake Tests/Unit/VirtualShadowTests.cpp
git commit -m "render: introduce render view families"
```

## Task 3: Split Scene Invalidation From View Projection State

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- Test: `Tests/Unit/VirtualShadowTests.cpp`

- [ ] **Step 1: Replace per-view invalidation tests with scene-change and projection tests**

Add tests with this shape:

```cpp
bool TestSceneInvalidationProducesChangedBoundsOnce()
{
    ve::VirtualShadowInvalidationTracker tracker;
    const ve::Aabb a = ve::Aabb::FromCenterExtents(ve::Vector3::Zero(), ve::Vector3::One());
    const ve::Aabb b = ve::Aabb::FromCenterExtents(ve::Vector3(5.0f, 0.0f, 0.0f), ve::Vector3::One());
    const ve::VirtualShadowCasterSnapshot first[] = {{7, 1, a, true}};
    const ve::VirtualShadowCasterSnapshot moved[] = {{7, 2, b, true}};

    const auto added = tracker.UpdateScene(1, ve::Vector3::UnitZ(), first);
    const auto changed = tracker.UpdateScene(2, ve::Vector3::UnitZ(), moved);
    const auto lightChanged = tracker.UpdateScene(3, ve::Vector3::UnitX(), moved);

    bool passed = true;
    passed &= Expect(added.changedBounds.size() == 1, "A new caster should expose its current bounds once");
    passed &= Expect(changed.changedBounds.size() == 2, "A moved caster should expose old and new bounds");
    passed &= Expect(lightChanged.lightBasisChanged, "A light-basis change should be scene-wide");
    return passed;
}
```

Add:

```cpp
bool TestViewProjectionCompatibility()
{
    ve::VirtualShadowViewCache cache;
    ve::VirtualShadowPrepareInput input = {};
    input.frameIndex = 1;
    input.viewID = 9;
    input.screenWidth = 1280;
    input.screenHeight = 720;
    input.viewProjection = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f);
    input.cameraLocalToWorld = ve::Matrix44::Identity();
    input.light = {true, ve::Vector3::UnitZ(), 200.0f, 0.001f, 0.05f};

    const auto first = cache.PrepareFrame(input);
    input.frameIndex = 2;
    input.cameraLocalToWorld = ve::Matrix44::Translation(ve::Vector3(first.clipmaps.levels[0].pageWorldSize, 0.0f, 0.0f));
    const auto panned = cache.PrepareFrame(input);
    input.frameIndex = 3;
    input.cameraLocalToWorld = ve::Matrix44::Translation(ve::Vector3(0.0f, 0.0f, first.clipmaps.depthStep * 1.1f));
    const auto depthChanged = cache.PrepareFrame(input);

    bool passed = true;
    passed &= Expect(first.projectionRevision == panned.projectionRevision,
                     "Page-aligned XY panning should retain projection compatibility");
    passed &= Expect(first.clipmaps.levels[0].originPageX != panned.clipmaps.levels[0].originPageX ||
                         first.clipmaps.levels[0].originPageY != panned.clipmaps.levels[0].originPageY,
                     "The compatibility test should actually move a clipmap origin");
    passed &= Expect(depthChanged.projectionRevision > panned.projectionRevision && depthChanged.invalidateViewPages,
                     "A depth-anchor change should invalidate only that view's content");
    return passed;
}
```

- [ ] **Step 2: Run the focused test and verify it fails**

Expected: compilation fails because `changedBounds`, `lightBasisChanged`, `projectionRevision`, and `invalidateViewPages` are missing.

- [ ] **Step 3: Refactor the invalidation tracker**

Add a scene-level result alongside the existing per-view result:

```cpp
struct VirtualShadowSceneInvalidationResult
{
    bool lightBasisChanged = false;
    std::vector<Aabb> changedBounds;
};

class VirtualShadowInvalidationTracker
{
public:
    [[nodiscard]] VirtualShadowSceneInvalidationResult
    UpdateScene(UInt64 frameIndex, Vector3 lightDirection, std::span<const VirtualShadowCasterSnapshot> casters);
    void Clear() noexcept;
    [[nodiscard]] UInt32 GetTrackedCasterCount() const noexcept;
};
```

Keep the existing render-item revision/history rules, but append old/current `Aabb` values instead of projecting them through one view's clipmaps. Direction changes set `lightBasisChanged`. Retain the old per-view `Update` overloads temporarily so the current renderer compiles until Task 8 removes its old preparation path.

- [ ] **Step 4: Add the CPU-only preparation contract**

Add the new input and packet fields while retaining the old RHI members and entry point as a compilation bridge. The manager will use only the new CPU contract; Task 8 removes the transitional per-view GPU resources after all renderer consumers switch.

Use this preparation contract:

```cpp
struct VirtualShadowPrepareInput
{
    UInt64 frameIndex = 0;
    UInt32 viewID = InvalidVirtualShadowViewID;
    UInt32 screenWidth = 0;
    UInt32 screenHeight = 0;
    Matrix44 viewProjection = Matrix44::Identity();
    Matrix44 cameraLocalToWorld = Matrix44::Identity();
    VirtualShadowLightInput light;
    std::span<const Aabb> changedCasterBounds;
    bool resetSceneCache = false;
};

struct VirtualShadowFramePacket
{
    bool valid = true;
    bool enabled = false;
    UInt32 viewID = InvalidVirtualShadowViewID;
    UInt32 projectionRevision = 0;
    UInt32 atlasExtent = 0;
    Float32 depthBias = 0.0f;
    Float32 normalBias = 0.0f;
    VirtualShadowClipmapSet clipmaps;
    bool resetSceneCache = false;
    bool invalidateViewPages = false;
    UInt64 frameIndex = 0;
    UInt32 screenWidth = 0;
    UInt32 screenHeight = 0;
    Matrix44 inverseViewProjection = Matrix44::Identity();
    Vector4 cameraWorldPosition = Vector4::Zero();
    Vector4 cameraWorldForward = Vector4(0.0f, 0.0f, 1.0f, 0.0f);
    std::vector<VirtualShadowPageKey> invalidatedPageKeys;
};
```

For every changed bound, call `BuildVirtualShadowPageKeysForBounds(input.viewID, packet.clipmaps, bound)`. Compare a stored projection signature containing shadow distance, level page sizes, and every level's `depthEpoch`, but excluding `originPageX/Y`. On an incompatible signature, increment the revision and set `invalidateViewPages`; XY panning alone does neither. The transitional old `PrepareFrame` overload delegates to this implementation with its current view-local invalidation data.

- [ ] **Step 5: Run focused tests**

Run the focused build and test commands.

Expected: scene invalidation, XY origin reuse, depth-anchor invalidation, and existing clipmap tests pass.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp Tests/Unit/VirtualShadowTests.cpp
git commit -m "render: separate VSM scene and view invalidation"
```

## Task 4: Add The Per-Scene Shared Resource Cache

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
- Modify: `Engine/Runtime/Render/RenderViewState.h`
- Modify: `Engine/Runtime/Render/RenderViewState.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Test: `Tests/Unit/VirtualShadowTests.cpp`

- [ ] **Step 1: Add failing slice-allocation tests**

Add:

```cpp
bool TestVirtualShadowPageTableSlices()
{
    ve::VirtualShadowPageTableSliceAllocator allocator(2);
    const auto first = allocator.Acquire(11);
    const auto repeated = allocator.Acquire(11);
    const auto second = allocator.Acquire(12);
    const auto overflow = allocator.Acquire(13);
    allocator.Release(11);
    const auto reused = allocator.Acquire(13);

    bool passed = true;
    passed &= Expect(first.IsValid() && repeated.index == first.index, "One view should retain one stable slice");
    passed &= Expect(second.IsValid() && second.index != first.index, "Different views need isolated slices");
    passed &= Expect(!overflow.IsValid(), "Slice exhaustion should not alias another view");
    passed &= Expect(reused.IsValid() && reused.index == first.index, "A released slice should become reusable");
    return passed;
}
```

- [ ] **Step 2: Run the test and verify the missing type**

Expected: compilation fails because `VirtualShadowPageTableSliceAllocator` does not exist.

- [ ] **Step 3: Define cache configuration and slices**

Use:

```cpp
struct VirtualShadowSceneCacheDesc
{
    UInt32 atlasExtent = 4096;
    UInt32 maximumViewCount = 8;
};

struct VirtualShadowPageTableSlice
{
    UInt32 index = std::numeric_limits<UInt32>::max();
    UInt64 pageMarksOffset = 0;
    UInt64 pageTableOffset = 0;
    UInt64 requestListOffset = 0;
    UInt64 requestCountsOffset = 0;
    [[nodiscard]] bool IsValid() const noexcept;
};
```

`VirtualShadowPageTableSliceAllocator` maps `viewID` to a fixed slot and returns byte offsets computed from:

```cpp
constexpr UInt64 VirtualShadowLogicalPageBufferSize = UInt64(VirtualShadowLogicalPageCount) * sizeof(UInt32);
constexpr UInt64 VirtualShadowRequestCountBufferSize = UInt64(VirtualShadowClipmapLevelCount) * sizeof(UInt32);
```

Store the assigned slice on `RTRenderViewState` with getters and a manager-only setter. A view can change slices when it moves scenes; its `viewID` remains unchanged.

- [ ] **Step 4: Implement shared GPU resource ownership**

`VirtualShadowSceneCache` owns:

```cpp
std::unique_ptr<rhi::RhiTexture> atlasTexture_;
std::unique_ptr<rhi::RhiTexture> fallbackAtlasTexture_;
std::unique_ptr<rhi::RhiSampler> comparisonSampler_;
std::unique_ptr<rhi::RhiBuffer> pageMarksBuffer_;
std::unique_ptr<rhi::RhiBuffer> pageTableBuffer_;
std::unique_ptr<rhi::RhiBuffer> requestListBuffer_;
std::unique_ptr<rhi::RhiBuffer> requestCountsBuffer_;
std::unique_ptr<rhi::RhiBuffer> physicalPagesBuffer_;
VirtualShadowInvalidationTracker invalidationTracker_;
VirtualShadowPageTableSliceAllocator sliceAllocator_;
```

Allocate each view-sliced buffer as `maximumViewCount * sliceSize`. Allocate the physical buffer once from atlas capacity, and create one comparison sampler/fallback atlas per scene. `EnsureResources` is idempotent for one RHI device and marks `resetSceneCachePending_` after creation or recreation.

- [ ] **Step 5: Build and test**

Register the new files in `CMake/Targets/Engine.cmake`, then run the focused tests.

Expected: slice allocation and existing VSM tests pass.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp Engine/Runtime/Render/RenderViewState.h Engine/Runtime/Render/RenderViewState.cpp CMake/Targets/Engine.cmake Tests/Unit/VirtualShadowTests.cpp
git commit -m "render: add scene-shared VSM resources"
```

## Task 5: Migrate The GPU Pass To A Direct-RHI Pipeline

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Define the direct command API**

Create contexts containing explicit resources rather than FrameGraph handles:

```cpp
struct VirtualShadowGpuViewContext
{
    const FrameRenderPipelineData& frameData;
    const RenderView& view;
    const VirtualShadowFramePacket& packet;
    const VirtualShadowPageTableSlice& slice;
    VirtualShadowSceneCache& sceneCache;
    std::span<const std::shared_ptr<RTRenderItem>> opaqueItems;
};

class VirtualShadowGpuPipeline final : public NonCopyable
{
public:
    [[nodiscard]] ErrorCode ClearSceneFrame(const FrameRenderPipelineData& frameData, VirtualShadowSceneCache& cache, bool resetSceneCache);
    [[nodiscard]] ErrorCode PrepareViewRequests(const VirtualShadowGpuViewContext& context);
    [[nodiscard]] ErrorCode ResolveViewHits(const VirtualShadowGpuViewContext& context, UInt32 level);
    [[nodiscard]] ErrorCode AllocateViewPages(const VirtualShadowGpuViewContext& context, UInt32 level);
    [[nodiscard]] ErrorCode RenderViewPages(const VirtualShadowGpuViewContext& context);
    [[nodiscard]] ErrorCode FinalizeSceneFrame(const FrameRenderPipelineData& frameData, VirtualShadowSceneCache& cache);
};
```

- [ ] **Step 2: Move shader and pipeline creation code**

Move the embedded HLSL strings and graphics/compute pipeline builders from `GpuVirtualShadowRenderPass.cpp`. Replace `RenderPassContext` lookups with explicit fields from `VirtualShadowGpuViewContext`.

Direct raster recording uses:

```cpp
rhi::RhiCommandList& commandList = context.frameData.GetCommandList();
rhi::RhiRenderPassBeginInfo beginInfo = {};
beginInfo.debugName = "VirtualShadowReceiverDepth";
beginInfo.hasColorAttachment = false;
beginInfo.colorAttachmentIsSwapchain = false;
beginInfo.hasDepthAttachment = true;
beginInfo.depthAttachment.texture = context.view.target.colorTexture->GetDepthTexture();
beginInfo.depthAttachment.loadAction = rhi::RhiLoadAction::Clear;
beginInfo.depthAttachment.storeAction = rhi::RhiStoreAction::Store;
if (!commandList.BeginRenderPass(*context.frameData.mainSwapchain, beginInfo))
{
    return ErrorCode::PlatformError;
}
```

Always call `EndRenderPass` after a successful begin. The physical atlas pass uses the same shape with `sceneCache.GetAtlasTexture()` and `Load`.

- [ ] **Step 3: Bind view slices instead of per-view buffers**

Every marks/table/request binding uses the shared buffer plus the slice byte range:

```cpp
commandList.SetReadWriteStorageBuffer(rhi::RhiShaderStage::Compute,
                                      0,
                                      *context.sceneCache.GetPageTableBuffer(),
                                      context.slice.pageTableOffset,
                                      VirtualShadowLogicalPageBufferSize);
```

The shader continues indexing a local `0..VirtualShadowLogicalPageCount-1` range because the RHI binding starts at the slice offset.

- [ ] **Step 4: Update shader identity and family pinning**

Add `viewID` to `VirtualShadowGpuConstants`. In resolve/allocation HLSL build:

```hlsl
uint key1 = level | ((viewID & 0x00FFFFFFu) << 8u);
```

`ClearSceneFrame` clears the `Requested` flag once for the shared physical array. Per-view invalidation compares complete keys and, for `invalidateViewPages`, dirties only physical entries whose packed view ID equals `viewID`.

Resolve stages run before allocation stages at manager level. A resolve hit sets `Requested`; allocation never selects a page with `Requested`. A newly allocated page sets `Valid | Dirty | Requested`. This preserves every family view's hits before any miss can evict them.

The page-clear and caster vertex shaders reject physical entries whose packed view ID differs from the current constants. `FinalizeSceneFrame` clears `Dirty | Requested` only after every view has rendered its pages.

- [ ] **Step 5: Preserve deterministic coarse-to-fine behavior**

Expose one-level resolve/allocate methods. Do not put a view loop inside the GPU pipeline; the manager owns:

```text
all views: prepare requests
all views, coarse-to-fine: resolve hits
views in family order, coarse-to-fine: allocate misses
views in family order: render pages
finalize once
```

- [ ] **Step 6: Register the new pipeline alongside the old pass**

Add `VirtualShadowGpuPipeline.h/.cpp` to `CMake/Targets/Engine.cmake`. Keep `GpuVirtualShadowRenderPass` registered as a temporary compilation bridge until Task 8 switches the renderer and deletes it.

- [ ] **Step 7: Compile the engine**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngine
```

Expected: `VEngine` builds with both the old renderer pass and the new unused direct-RHI pipeline.

- [ ] **Step 8: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.h Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h CMake/Targets/Engine.cmake
git commit -m "render: move VSM preparation to direct RHI"
```

## Task 6: Implement VirtualShadowManager Family Orchestration

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Test: `Tests/Unit/VirtualShadowTests.cpp`

- [ ] **Step 1: Add failing view-ID allocator tests**

Add:

```cpp
bool TestVirtualShadowManagerViewIDs()
{
    ve::VirtualShadowManager manager;
    auto first = std::make_shared<ve::RTRenderViewState>(ve::RenderViewStateDesc{"First"});
    auto second = std::make_shared<ve::RTRenderViewState>(ve::RenderViewStateDesc{"Second"});

    const ve::UInt32 firstID = manager.GetOrAssignViewID(*first);
    const ve::UInt32 repeatedID = manager.GetOrAssignViewID(*first);
    const ve::UInt32 secondID = manager.GetOrAssignViewID(*second);

    bool passed = true;
    passed &= Expect(firstID != 0 && firstID == repeatedID, "One persistent view should keep one ID");
    passed &= Expect(secondID != 0 && secondID != firstID, "Different persistent views should receive different IDs");
    return passed;
}
```

- [ ] **Step 2: Run the focused test and verify it fails**

Expected: compilation fails because `VirtualShadowManager` is missing.

- [ ] **Step 3: Define the immutable result and manager**

Use:

```cpp
struct VirtualShadowViewResult
{
    bool enabled = false;
    bool receiverDepthReady = false;
    VirtualShadowFramePacket packet;
    rhi::RhiTexture* atlas = nullptr;
    rhi::RhiTexture* fallbackAtlas = nullptr;
    rhi::RhiSampler* comparisonSampler = nullptr;
    rhi::RhiBuffer* pageTable = nullptr;
    UInt64 pageTableOffset = 0;
    UInt64 pageTableSize = 0;
};

class VirtualShadowManager final : public NonCopyable
{
public:
    explicit VirtualShadowManager(VirtualShadowSceneCacheDesc desc = {});
    [[nodiscard]] UInt32 GetOrAssignViewID(RTRenderViewState& viewState) noexcept;
    [[nodiscard]] ErrorCode PreRenderShadowStep(const FrameRenderPipelineData& frameData, RenderViewFamily& family);
    void CollectExpiredState();
};
```

Internally store a monotonic `nextViewID_`, a `RTScene* -> SceneEntry` map with weak scene ownership, and weak view registrations used to release slices after view destruction.

Use this private prepared-work item so every GPU stage references the same immutable packet and slice:

```cpp
struct PreparedView
{
    VirtualShadowGpuViewContext context;
    std::shared_ptr<VirtualShadowViewResult> result;
};
```

- [ ] **Step 4: Build family-shared scene inputs**

Scan `family.scene` once to produce:

```cpp
std::vector<std::shared_ptr<RTRenderItem>> opaqueItems;
std::vector<VirtualShadowCasterSnapshot> casterSnapshots;
```

Opaque receiver items follow `RenderQueue::Opaque`. Caster snapshots include opaque items and their render ID, revision, world bounds, and `CastShadows` value. Evaluate the scene cache's invalidation tracker once, then pass its changed bounds to every view's CPU cache.

- [ ] **Step 5: Implement the family stage order**

The method must:

```cpp
pipeline_.ClearSceneFrame(frameData, cache, resetSceneCache);
for (PreparedView& view : preparedViews)
{
    pipeline_.PrepareViewRequests(view.context);
}
for (UInt32 coarseIteration = 0; coarseIteration < VirtualShadowClipmapLevelCount; ++coarseIteration)
{
    const UInt32 level = VirtualShadowClipmapLevelCount - 1u - coarseIteration;
    for (PreparedView& view : preparedViews)
    {
        pipeline_.ResolveViewHits(view.context, level);
    }
}
for (PreparedView& view : preparedViews)
{
    for (UInt32 coarseIteration = 0; coarseIteration < VirtualShadowClipmapLevelCount; ++coarseIteration)
    {
        const UInt32 level = VirtualShadowClipmapLevelCount - 1u - coarseIteration;
        pipeline_.AllocateViewPages(view.context, level);
    }
}
for (PreparedView& view : preparedViews)
{
    pipeline_.RenderViewPages(view.context);
}
pipeline_.FinalizeSceneFrame(frameData, cache);
```

Prepare all valid views before recording these stages. Assign each result to `family.views[index].virtualShadowResult`. A failed view produces a disabled result and is skipped without dropping other views. A scene-resource failure produces disabled results for the entire family and returns `ErrorCode::None` so normal rendering continues.

- [ ] **Step 6: Build and test**

Register the manager files, build `VEngineVirtualShadowTests`, and run the focused CTest.

Expected: stable view-ID and existing CPU tests pass.

- [ ] **Step 7: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp CMake/Targets/Engine.cmake Tests/Unit/VirtualShadowTests.cpp
git commit -m "render: orchestrate VSM view families"
```

## Task 7: Connect The Manager To RenderSystem And Frame Pipelines

**Files:**

- Modify: `Engine/Runtime/Render/RenderFramePipelineData.h`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.h`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.cpp`
- Modify: `Editor/Core/Editor.cpp`
- Modify: `Engine/Runtime/Scene/SceneSystem.cpp`

- [ ] **Step 1: Publish render-system ownership**

Add:

```cpp
std::unique_ptr<VirtualShadowManager> virtualShadowManager;
```

to `RenderSystemImpl`. Create it after device creation succeeds and reset it before device destruction. Add `VirtualShadowManager* virtualShadowManager = nullptr;` to `FrameRenderPipelineData`, populated in `PrepareMainSwapchainFrame`.

- [ ] **Step 2: Build one Editor family before any renderer**

In `EditorRenderFramePipeline::RenderFrame`, validate that all current Scene/Game renderer entries reference the same scene, then copy their `RenderView` values into:

```cpp
RenderViewFamily family = {};
family.scene = sceneRenderers_.empty() ? nullptr : sceneRenderers_.front().scene;
for (const StandaloneRendererInitParam& initParam : sceneRenderers_)
{
    if (initParam.scene != family.scene)
    {
        const bool ended = commandList.End();
        VE_ASSERT_MESSAGE(ended, "EditorRenderFramePipeline failed to end a mismatched-scene family.");
        return ErrorCode::InvalidArgument;
    }
    family.views.push_back(initParam.view);
}
```

Call `PreRenderShadowStep` exactly once after `commandList.Begin()` and before constructing the first `StandaloneRenderer`. Copy or move each prepared `family.views[index]` back into the corresponding init parameter before invoking that renderer.

- [ ] **Step 3: Build the one-view Player family**

After `EnsureSceneColorTexture` and `commandList.Begin`, create:

```cpp
RenderViewFamily family = {};
family.scene = sceneRenderer_.scene;
family.views.push_back(sceneRenderer_.view);
```

Run the same manager step, then move `family.views.front()` back to `sceneRenderer_.view` before `CreatePlayerRenderer`.

- [ ] **Step 4: Preserve recoverable shadow failure**

If the manager pointer is null, attach disabled results and continue:

```cpp
if (frameData.virtualShadowManager != nullptr)
{
    const ErrorCode shadowResult = frameData.virtualShadowManager->PreRenderShadowStep(frameData, family);
    if (shadowResult != ErrorCode::None)
    {
        const bool ended = commandList.End();
        VE_ASSERT_MESSAGE(ended, "Frame pipeline failed to end after VSM preparation failure.");
        return shadowResult;
    }
}
else
{
    for (RenderView& view : family.views)
    {
        view.virtualShadowResult = std::make_shared<VirtualShadowViewResult>();
    }
}
```

Page exhaustion or unsupported Metal VSM must not return a frame failure.

- [ ] **Step 5: Build all Windows test targets**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: all test executables compile.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/RenderFramePipelineData.h Engine/Runtime/Render/RenderSystem.cpp Engine/Runtime/Render/RenderFramePipeline.h Engine/Runtime/Render/RenderFramePipeline.cpp Editor/Core/Editor.cpp Engine/Runtime/Scene/SceneSystem.cpp
git commit -m "render: prepare VSM before family views"
```

## Task 8: Make Single-View Renderers Consume Prepared Results

**Files:**

- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Delete: `Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.h`
- Delete: `Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.cpp`

- [ ] **Step 1: Remove renderer-side preparation**

Delete `BaseRenderer::PrepareVirtualShadows` and every call to per-view GPU resource creation. Initialize renderer data from:

```cpp
rendererData_.resolvedCamera = std::move(initParam.view.camera);
rendererData_.viewState = std::move(initParam.view.viewState);
rendererData_.virtualShadowResult = std::move(initParam.view.virtualShadowResult);
target_ = std::move(initParam.view.target);
```

Replace `RendererData::virtualShadowPacket` with `std::shared_ptr<VirtualShadowViewResult> virtualShadowResult`.

- [ ] **Step 2: Simplify graph resource imports**

Keep only:

```cpp
FrameGraphTextureHandle virtualShadowAtlas;
FrameGraphBufferHandle virtualShadowPageTable;
UInt64 virtualShadowPageTableOffset = 0;
UInt64 virtualShadowPageTableSize = 0;
bool depthPrepassReady = false;
```

Remove marks, request-list, request-count, and physical-page handles from `RendererFrameGraphData`.

`ImportVirtualShadowResources` imports the atlas and page-table buffer from `VirtualShadowViewResult`, stores the slice offset/size, and sets `depthPrepassReady = result.receiverDepthReady`.

- [ ] **Step 3: Bind the prepared result in scene passes**

In opaque and transparent draw paths, build constants from `result.packet`. Bind:

```cpp
commandList.SetStorageBuffer(rhi::RhiShaderStage::Fragment,
                             3,
                             *table.buffer,
                             context.rendererData.virtualShadowResult->pageTableOffset,
                             context.rendererData.virtualShadowResult->pageTableSize);
commandList.SetTexture(rhi::RhiShaderStage::Fragment, 1, *atlas.texture);
commandList.SetSampler(rhi::RhiShaderStage::Fragment,
                       1,
                       *context.rendererData.virtualShadowResult->comparisonSampler);
```

Do not read a sampler or atlas from `RenderViewState`. Disabled results upload constants with `enabled == 0` and use the manager's fallback bindings when the backend requires resources to be bound.

- [ ] **Step 4: Remove the old renderer pass**

Remove the include and `gpuVirtualShadowPass_` member from `StandaloneRenderer.h`. `StandaloneRenderer::BuildFrameGraph` starts with `opaquePass_.AddToFrameGraph`.

Delete `GpuVirtualShadowRenderPass.h/.cpp` and remove both entries from `CMake/Targets/Engine.cmake`. Remove the transitional per-view RHI resources and old `PrepareFrame`/`Update` overloads from `VirtualShadowViewCache` and `VirtualShadowInvalidationTracker`; only manager-driven CPU state and scene-cache GPU resources remain.

- [ ] **Step 5: Run focused and full automated tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: all existing CTest executables pass.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/Renderer/BaseRenderer.h Engine/Runtime/Render/Renderer/BaseRenderer.cpp Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h Engine/Runtime/Render/Renderer/StandaloneRenderer.h Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.h Engine/Runtime/Render/Renderer/RenderPass/GpuVirtualShadowRenderPass.cpp CMake/Targets/Engine.cmake
git commit -m "render: consume family-prepared VSM results"
```

## Task 9: Verify Clipmap Reuse, Isolation, And Architecture

**Files:**

- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `Docs/ArchitectureOverview.md`

- [ ] **Step 1: Add the final CPU contract checks**

Add:

```cpp
bool TestAbsolutePageReuseAcrossClipmapPanning()
{
    constexpr ve::Float32 ShadowDistance = 200.0f;
    const ve::Vector3 lightDirection = ve::Vector3::UnitZ();
    const auto first = ve::BuildVirtualShadowClipmaps(ve::Matrix44::Identity(), lightDirection, ShadowDistance);
    const auto moved = ve::BuildVirtualShadowClipmaps(
        ve::Matrix44::Translation(ve::Vector3(first.levels[0].pageWorldSize, 0.0f, 0.0f)), lightDirection, ShadowDistance);
    const ve::Aabb overlapBounds = ve::Aabb::FromCenterExtents(ve::Vector3::Zero(), ve::Vector3(0.1f, 0.1f, 0.1f));
    const auto firstKeys = ve::BuildVirtualShadowPageKeysForBounds(9, first, overlapBounds);
    const auto movedKeys = ve::BuildVirtualShadowPageKeysForBounds(9, moved, overlapBounds);
    const auto otherViewKeys = ve::BuildVirtualShadowPageKeysForBounds(10, moved, overlapBounds);

    bool passed = true;
    passed &= Expect(!firstKeys.empty() && firstKeys == movedKeys,
                     "Overlapping absolute pages should survive clipmap-origin panning");
    passed &= Expect(firstKeys != otherViewKeys, "Different view IDs should keep logical ownership isolated");
    return passed;
}
```

Keep the slice allocator and manager-ID tests in `main`.

- [ ] **Step 2: Update the architecture overview**

In the rendering architecture section, record:

```text
RenderSystem-owned VirtualShadowManager
  -> one VirtualShadowSceneCache per RTScene
  -> one family-wide PreRenderShadowStep
  -> shared physical atlas and allocator
  -> per-view page-table slices
  -> single-view BaseRenderer invocations
```

State that the page key is `(viewID, level, absolutePageX, absolutePageY)`, Clipmap Origin panning reuses overlapping absolute keys, and view-priority rotation is not implemented.

- [ ] **Step 3: Run source-contract checks**

Run:

```powershell
rg -n "GpuVirtualShadowRenderPass|GetAtlasTexture|GetGpuPhysicalPagesBuffer|PrepareVirtualShadows" Engine Editor
```

Expected: no obsolete per-view ownership or render-pass matches.

Run:

```powershell
rg -n "PreRenderShadowStep|VirtualShadowSceneCache|virtualShadowPageTableOffset|GetViewID" Engine Tests
```

Expected: manager ownership, family preparation, slice binding, and view-scoped keys all have matches.

- [ ] **Step 4: Run full build and tests**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: all commands exit successfully.

- [ ] **Step 5: Run D3D11 and D3D12 rendering acceptance**

Launch:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

For each Windows backend:

- Open Scene View and Game View simultaneously.
- Confirm both receive shadows.
- Enable the Scene View VSM page visualization and confirm Game View remains unaffected.
- Pan the Scene camera smoothly and confirm cached pages remain while narrow edge strips appear.
- Move a caster and confirm both old and new shadow coverage updates.
- Confirm no checkerboard, cross-view page corruption, or VSM overflow warning appears.

- [ ] **Step 6: Run final repository checks**

```text
git diff --check
git status --short
```

Expected: no whitespace errors; status contains only intentional implementation/documentation changes.

- [ ] **Step 7: Commit**

```text
git add Tests/Unit/VirtualShadowTests.cpp Docs/ArchitectureOverview.md
git commit -m "docs: document scene-shared VSM architecture"
```
