# Editor Performance Footer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Editor's one-line status bar with a 74-pixel, four-region performance footer and publish scene/view-family-wide VSM page statistics from the latest completed GPU frame without introducing a synchronous GPU wait.

**Architecture:** `VirtualShadowSceneCache` owns one compact GPU counter buffer per scene. `VirtualShadowManager` owns one readback buffer per in-flight frame slot, records the final GPU copy, and consumes it only after `RenderSystem` has waited for that slot's existing `FrameContext` fence. `RenderSystem` publishes a scene-aware, value-only snapshot through a small locked exchange. The Editor reads that snapshot and combines it with the existing one-second FPS average in a pure footer model before drawing an ImGui table.

**Tech Stack:** C++20, VEngine common RHI, embedded HLSL compute shaders, D3D11, D3D12, Metal compatibility implementation, Dear ImGui, CMake, CTest.

---

## File Structure

### New files

- `Engine/Runtime/Render/RenderPerformanceStatistics.h`
  - Defines the value-only render/VSM statistics snapshot and the thread-safe latest-snapshot exchange.
- `Engine/Runtime/Render/RenderPerformanceStatistics.cpp`
  - Implements scene activation, monotonic publication, and locked snapshot reads.
- `Editor/Core/EditorPerformanceFooterModel.h`
  - Defines the 74-pixel layout constants, four column weights, and pure footer presentation model.
- `Tests/Unit/RenderPerformanceStatisticsTests.cpp`
  - Covers snapshot publication, stale-scene rejection, unavailable data, and footer model mapping.
- `CMake/Targets/Tests/RenderPerformanceStatisticsTests.cmake`
  - Registers the focused statistics/footer-model test executable.

### Modified files

- `Engine/RHI/Common/RhiTypes.h`
  - Adds explicit GPU-to-CPU buffer memory usage.
- `Engine/RHI/Common/RhiDevice.h`
  - Adds bounded buffer copy and completed-readback operations.
- `Engine/RHI/D3D11/D3D11Rhi.cpp`
  - Implements staging buffers, buffer copies, and mapped reads.
- `Engine/RHI/D3D12/D3D12Rhi.cpp`
  - Implements readback heaps, state-aware buffer copies, and mapped reads.
- `Engine/RHI/Metal/MetalRhi.mm`
  - Implements the common APIs with shared buffers and a blit encoder; VSM itself remains unsupported.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
  - Defines the CPU/GPU counter ABI and counter indices.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
  - Creates and exposes the per-scene GPU statistics buffer.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
  - Clears, increments, finalizes, and copies statistics while preserving the existing VSM rendering behavior.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
  - Owns readback slots, frame/scene identities, and best-effort sample consumption.
- `Engine/Runtime/Render/RenderSystem.h`
- `Engine/Runtime/Render/RenderSystem.cpp`
  - Publishes completed statistics and exposes the Main Thread snapshot getter.
- `Editor/Core/EditorProjectEditingView.cpp`
  - Draws the 74-pixel four-column footer.
- `CMake/Targets/Engine.cmake`
- `CMake/Targets/Applications/Common.cmake`
- `CMake/Targets/Tests.cmake`
  - Registers the new engine source, Editor model header, and focused test.
- `Tests/Unit/ResourceRenderTests.cpp`
- `Tests/Unit/VirtualShadowTests.cpp`
- `Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp`
  - Updates mock RHI implementations and verifies recording/readback behavior.
- `Docs/ArchitectureOverview.md`
  - Records the completed-frame performance-statistics publication boundary.

### Explicitly unchanged

- `Editor/Windows/main.cpp`
  - The user's 850-pixel Editor height remains untouched.
- `Engine/Runtime/Render/FrameContext.h`
  - No new state is added. `RenderSystemImpl` tracks which frame used each existing frame-context slot.

## Task 1: Define The Value Snapshot And Footer Model

**Files:**

- Create: `Engine/Runtime/Render/RenderPerformanceStatistics.h`
- Create: `Engine/Runtime/Render/RenderPerformanceStatistics.cpp`
- Create: `Editor/Core/EditorPerformanceFooterModel.h`
- Create: `Tests/Unit/RenderPerformanceStatisticsTests.cpp`
- Create: `CMake/Targets/Tests/RenderPerformanceStatisticsTests.cmake`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `CMake/Targets/Applications/Common.cmake`
- Modify: `CMake/Targets/Tests.cmake`

- [ ] **Step 1: Add failing snapshot and footer-model tests**

Create `RenderPerformanceStatisticsTests.cpp` with focused cases for:

```cpp
bool TestStatisticsExchangeRejectsStaleScenes();
bool TestStatisticsExchangeRejectsOlderFrames();
bool TestUnavailableSnapshot();
bool TestFooterLayoutAndModel();
```

The main contract used by the tests is:

```cpp
ve::RenderPerformanceStatisticsExchange exchange;

exchange.ActivateScene(101, 8);
exchange.Publish(MakeAvailableStatistics(7, 99));
Expect(!exchange.GetLatest().virtualShadow.available,
       "A completed sample from the previous scene must not become visible");

exchange.Publish(MakeAvailableStatistics(8, 101));
const ve::RenderPerformanceStatistics latest = exchange.GetLatest();
Expect(latest.sourceFrameIndex == 8 && latest.sceneIdentity == 101,
       "The active scene's completed sample should be published");
```

Verify the Editor model constants and value mapping:

```cpp
static_assert(ve::editor::EditorPerformanceFooterHeight == 74.0F);
static_assert(ve::editor::CommonFooterColumnWeight == 0.18F);
static_assert(ve::editor::RenderFooterColumnWeight == 0.18F);
static_assert(ve::editor::VirtualShadowFooterColumnWeight == 0.46F);
static_assert(ve::editor::PhysicsFooterColumnWeight == 0.18F);

const auto model = ve::editor::BuildEditorPerformanceFooterModel(60.0F, latest);
Expect(model.framesPerSecond == 60.0F && model.virtualShadow.available,
       "Footer model should combine TimeSystem FPS with the RenderSystem snapshot");
```

- [ ] **Step 2: Register and run the focused test to verify it fails**

Add `RenderPerformanceStatisticsTests.cmake` to `CMake/Targets/Tests.cmake`, define `VEngineRenderPerformanceStatisticsTests`, and run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRenderPerformanceStatisticsTests
```

Expected: compilation fails because the statistics and footer model types do not exist.

- [ ] **Step 3: Implement the value contract**

Define these public value types in `RenderPerformanceStatistics.h`:

```cpp
struct VirtualShadowPerformanceStatistics
{
    bool available = false;
    UInt32 totalPhysicalPages = 0;
    UInt32 allocatedPhysicalPages = 0;
    UInt32 requestedPages = 0;
    UInt32 cachedPages = 0;
    UInt32 newlyAllocatedPages = 0;
    UInt32 redrawnPages = 0;
    UInt32 unmappedPages = 0;
};

struct RenderPerformanceStatistics
{
    UInt64 sourceFrameIndex = 0;
    UInt64 sceneIdentity = 0;
    VirtualShadowPerformanceStatistics virtualShadow;
};
```

Implement a small locked exchange:

```cpp
class RenderPerformanceStatisticsExchange final : public NonCopyable
{
public:
    void ActivateScene(UInt64 sceneIdentity, UInt64 sourceFrameIndex);
    void Publish(const RenderPerformanceStatistics& statistics);
    [[nodiscard]] RenderPerformanceStatistics GetLatest() const;

private:
    mutable Mutex mutex_;
    RenderPerformanceStatistics latest_;
    UInt64 activeSceneIdentity_ = 0;
};
```

`ActivateScene` publishes an unavailable value when the scene identity changes. `Publish` accepts only the active scene and never replaces a newer source frame with an older one. The exchange contains values only; it must not store `RTScene`, cache, RHI, or Editor pointers.

- [ ] **Step 4: Implement the pure Editor model**

`EditorPerformanceFooterModel.h` should be free of ImGui dependencies:

```cpp
inline constexpr Float32 EditorPerformanceFooterHeight = 74.0F;
inline constexpr Float32 CommonFooterColumnWeight = 0.18F;
inline constexpr Float32 RenderFooterColumnWeight = 0.18F;
inline constexpr Float32 VirtualShadowFooterColumnWeight = 0.46F;
inline constexpr Float32 PhysicsFooterColumnWeight = 0.18F;

struct EditorPerformanceFooterModel
{
    Float32 framesPerSecond = 0.0F;
    VirtualShadowPerformanceStatistics virtualShadow;
};

[[nodiscard]] inline EditorPerformanceFooterModel
BuildEditorPerformanceFooterModel(Float32 framesPerSecond, const RenderPerformanceStatistics& statistics) noexcept;
```

Keep Render and Physics as UI placeholders rather than inventing counters.

- [ ] **Step 5: Run the focused test**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRenderPerformanceStatisticsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineRenderPerformanceStatisticsTests --output-on-failure
```

Expected: build and test pass.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/RenderPerformanceStatistics.h Engine/Runtime/Render/RenderPerformanceStatistics.cpp Editor/Core/EditorPerformanceFooterModel.h Tests/Unit/RenderPerformanceStatisticsTests.cpp CMake/Targets/Tests/RenderPerformanceStatisticsTests.cmake CMake/Targets/Engine.cmake CMake/Targets/Applications/Common.cmake CMake/Targets/Tests.cmake
git commit -m "render: define performance statistics snapshots"
```

## Task 2: Add Common RHI Buffer Readback

**Files:**

- Modify: `Engine/RHI/Common/RhiTypes.h`
- Modify: `Engine/RHI/Common/RhiDevice.h`
- Modify: `Engine/RHI/D3D11/D3D11Rhi.cpp`
- Modify: `Engine/RHI/D3D12/D3D12Rhi.cpp`
- Modify: `Engine/RHI/Metal/MetalRhi.mm`
- Modify: `Tests/Unit/ResourceRenderTests.cpp`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp`

- [ ] **Step 1: Add failing API and validation tests**

Extend `ResourceRenderTests.cpp` to validate:

- `GpuToCpu` buffers reject initial data and storage/UAV use.
- `CopyBuffer` rejects zero-sized and out-of-bounds ranges.
- `ReadBuffer` rejects non-readback buffers and out-of-bounds destinations.

Add forwarding/stub methods to test devices only after the build has demonstrated every affected implementation site.

- [ ] **Step 2: Add the common API and verify all derived classes fail to compile**

Add:

```cpp
enum class RhiBufferMemoryUsage
{
    GpuOnly,
    CpuToGpu,
    GpuToCpu,
};
```

Add to `RhiCommandList`:

```cpp
[[nodiscard]] virtual bool
CopyBuffer(RhiBuffer& source, uint64_t sourceOffset, RhiBuffer& destination, uint64_t destinationOffset, uint64_t size) = 0;
```

Add to `RhiDevice`:

```cpp
[[nodiscard]] virtual bool
ReadBuffer(const RhiBuffer& buffer, uint64_t offset, void* destination, uint64_t size) = 0;
```

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineResourceRenderTests
```

Expected: compilation identifies the D3D11, D3D12, Metal, recording command list, diagnostic device, forwarding device, and test-device overrides that must be implemented.

- [ ] **Step 3: Implement D3D11**

For `GpuToCpu`, create `D3D11_USAGE_STAGING`, no bind flags, and `D3D11_CPU_ACCESS_READ`. Implement bounded copies with `ID3D11DeviceContext::CopySubresourceRegion`; clear conflicting SRV/UAV bindings before copying. Implement `ReadBuffer` with `D3D11_MAP_READ` and `memcpy`, and always unmap on success.

- [ ] **Step 4: Implement D3D12**

For `GpuToCpu`, create a `D3D12_HEAP_TYPE_READBACK` resource in `D3D12_RESOURCE_STATE_COPY_DEST`. Extend `D3D12Buffer` state tracking to all three memory usages.

`CopyBuffer` must:

1. Validate both ranges.
2. Transition the source to `D3D12_RESOURCE_STATE_COPY_SOURCE`.
3. Keep/transition the destination to `D3D12_RESOURCE_STATE_COPY_DEST`.
4. Call `CopyBufferRegion`.
5. Update the tracked states.

`ReadBuffer` maps only a `GpuToCpu` buffer, uses the requested `D3D12_RANGE`, copies the bytes, and unmaps with an empty write range. It is called only after the owning fence is complete.

- [ ] **Step 5: Implement Metal and all test doubles**

Use shared storage for `GpuToCpu`, record copies with an `MTLBlitCommandEncoder`, and read with `contents`. This keeps the common RHI complete even though `VirtualShadowGpuPipeline` still returns `Unsupported` on Metal.

Update:

- `DiagnosticMetalDevice`
- `FailFirstBufferDevice`
- `TestShaderDevice`
- `RecordingCommandList`

Forwarding devices delegate `ReadBuffer`; the recording command list stores the copy range for later ordering assertions.

- [ ] **Step 6: Add real-backend copy/read tests**

In `VirtualShadowGpuPipelineSmokeTests.cpp`, for D3D11 and D3D12:

1. Produce known GPU buffer data.
2. Record `CopyBuffer` to a buffer whose usage is `Readback` and whose memory usage is `GpuToCpu`.
3. Submit with the existing `FrameContext` fence.
4. Call `WaitAndReset`.
5. Call `ReadBuffer` and compare exact bytes.

The test must not call `WaitIdle` between submit and read; it should prove the frame-context fence is sufficient.

- [ ] **Step 7: Run focused tests**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineResourceRenderTests VEngineVirtualShadowGpuPipelineSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R "VEngine(ResourceRender|VirtualShadowGpuPipelineSmoke)Tests" --output-on-failure
```

Expected: both tests pass on D3D11 and D3D12.

- [ ] **Step 8: Commit**

```text
git add Engine/RHI/Common/RhiTypes.h Engine/RHI/Common/RhiDevice.h Engine/RHI/D3D11/D3D11Rhi.cpp Engine/RHI/D3D12/D3D12Rhi.cpp Engine/RHI/Metal/MetalRhi.mm Tests/Unit/ResourceRenderTests.cpp Tests/Unit/VirtualShadowTests.cpp Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp
git commit -m "rhi: add completed buffer readback"
```

## Task 3: Collect Scene-Wide VSM Counters On The GPU

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp`

- [ ] **Step 1: Add failing GPU ABI and recording-order tests**

Define test expectations before production code:

```cpp
static_assert(sizeof(ve::VirtualShadowGpuStatistics) == 32);
static_assert(alignof(ve::VirtualShadowGpuStatistics) == 16);

Expect(recordingCommandList.statisticsClearBeforeRequestDispatch,
       "Family statistics must clear before any view contributes requests");
Expect(recordingCommandList.statisticsCopyAfterFinalizeDispatch,
       "Statistics must be copied only after allocated-page finalization");
```

Also assert one statistics buffer is shared by every slice acquired from one `VirtualShadowSceneCache`.

- [ ] **Step 2: Run the focused test and verify the missing ABI/resources**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests
```

Expected: compilation fails on the missing GPU statistics type and scene-cache accessor.

- [ ] **Step 3: Define one stable 32-byte CPU/GPU ABI**

Use explicit counter indices and padding:

```cpp
enum class VirtualShadowGpuStatisticIndex : UInt32
{
    AllocatedPhysicalPages,
    RequestedPages,
    CachedPages,
    NewlyAllocatedPages,
    RedrawnPages,
    UnmappedPages,
    Count,
};

struct alignas(16) VirtualShadowGpuStatistics
{
    UInt32 allocatedPhysicalPages = 0;
    UInt32 requestedPages = 0;
    UInt32 cachedPages = 0;
    UInt32 newlyAllocatedPages = 0;
    UInt32 redrawnPages = 0;
    UInt32 unmappedPages = 0;
    UInt32 padding[2] = {};
};
```

Add static assertions for size, alignment, and field offsets so embedded HLSL counter indices cannot silently drift.

- [ ] **Step 4: Add the per-scene counter buffer**

`VirtualShadowSceneCache::EnsureResources` creates:

```cpp
CreateStorageBuffer(device,
                    sizeof(VirtualShadowGpuStatistics),
                    sizeof(UInt32),
                    "VirtualShadowSceneStatistics");
```

Expose `GetStatisticsBuffer()` const/non-const accessors, include it in complete-resource validation, and release it with the rest of the scene cache.

- [ ] **Step 5: Update existing shader stages**

Bind `RWStructuredBuffer<uint> Statistics` and atomically update:

- `ClearSceneComputeHlsl`: clear all six counters once per family.
- `CompactRequestsComputeHlsl`: increment `RequestedPages` once for every compacted logical request.
- `ResolvePageHitsComputeHlsl`: increment `CachedPages` when a resident key is resolved.
- `AllocateComputeHlsl`: increment `NewlyAllocatedPages` for a new/repurposed assignment and `UnmappedPages` when no unpinned physical page exists.
- `MarkRenderedComputeHlsl`: increment `RedrawnPages` exactly when a dirty requested page is marked rendered.
- `FinalizeComputeHlsl`: increment `AllocatedPhysicalPages` for each valid physical page after frame state is finalized.

Do not add a separate metadata scan or change allocation order. Preserve:

```text
requestedPages = cachedPages + newlyAllocatedPages + unmappedPages
allocatedPhysicalPages <= totalPhysicalPages
```

- [ ] **Step 6: Add a best-effort copy method**

Add:

```cpp
[[nodiscard]] ErrorCode CopySceneStatistics(const FrameRenderPipelineData& frameData,
                                            VirtualShadowSceneCache& sceneCache,
                                            rhi::RhiBuffer& readbackBuffer);
```

It records one exact-size `CopyBuffer` after `FinalizeSceneFrame`. A copy failure reports an unavailable sample to the manager later; it must not disable VSM rendering.

- [ ] **Step 7: Extend real GPU smoke assertions**

Submit the recorded VSM stages, wait via the frame-context fence, read the counter struct, and assert:

```cpp
statistics.allocatedPhysicalPages <= sceneCache.GetPhysicalPageCapacity();
statistics.requestedPages ==
    statistics.cachedPages + statistics.newlyAllocatedPages + statistics.unmappedPages;
statistics.redrawnPages <= statistics.allocatedPhysicalPages;
```

Use actual readback values on both D3D11 and D3D12.

- [ ] **Step 8: Run focused tests**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineVirtualShadowGpuPipelineSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R "VEngineVirtualShadow(Tests|GpuPipelineSmokeTests)" --output-on-failure
```

Expected: unit and real-backend smoke tests pass.

- [ ] **Step 9: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.h Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp Tests/Unit/VirtualShadowTests.cpp Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp
git commit -m "render: collect scene-wide VSM GPU statistics"
```

## Task 4: Read Completed Slots And Publish Through RenderSystem

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/RenderSystem.h`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `Tests/Unit/RenderPerformanceStatisticsTests.cpp`
- Modify: `Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp`

- [ ] **Step 1: Add failing manager lifecycle tests**

Cover these state transitions:

```text
record unavailable/available sample
    -> successful queue submit
    -> commit statistics slot
    -> wait existing FrameContext fence
    -> consume exact source frame
```

Also cover:

- Abort before submit does not publish.
- A no-light family commits an unavailable sample.
- A readback failure returns unavailable and does not change the render result.
- A scene-token change invalidates the old visible sample immediately.
- A late sample from the prior scene is rejected.

- [ ] **Step 2: Run the focused tests and verify the manager API is missing**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineRenderPerformanceStatisticsTests
```

Expected: compilation fails on the missing frame-statistics lifecycle.

- [ ] **Step 3: Add manager-owned readback slots**

In `VirtualShadowManager::Impl`, add two slots matching `RenderFrameContextCount`. Each slot owns:

```cpp
struct StatisticsReadbackSlot
{
    std::unique_ptr<rhi::RhiBuffer> buffer;
    UInt64 frameIndex = 0;
    UInt64 sceneIdentity = 0;
    UInt32 totalPhysicalPages = 0;
    bool recorded = false;
    bool submitted = false;
};
```

Assign every live `SceneEntry` a monotonically increasing non-zero `sceneIdentity`. Never derive identity solely from the `RTScene*` address, because allocator reuse could make an expired scene look current.

Expose an internal render-thread lifecycle equivalent to:

```cpp
[[nodiscard]] UInt64 GetRecordingSceneIdentity(UInt64 frameIndex) const noexcept;
void CommitFrameStatistics(UInt64 frameIndex) noexcept;
void AbortFrameStatistics(UInt64 frameIndex) noexcept;
[[nodiscard]] std::optional<RenderPerformanceStatistics>
ConsumeCompletedFrameStatistics(rhi::RhiDevice& device, UInt64 frameIndex) noexcept;
```

`PreRenderShadowStep` initializes the current slot as unavailable before checking Metal support, light availability, view validity, or statistics resource creation. When finalization succeeds, it records the GPU counter copy and marks the slot `recorded`.

- [ ] **Step 4: Track submitted frame indices beside FrameContext**

Add to `RenderSystemImpl`:

```cpp
std::array<UInt64, RenderFrameContextCount> submittedFrameIndices{};
RenderPerformanceStatisticsExchange performanceStatistics;
```

In `PrepareMainSwapchainFrame`:

1. Select `slotIndex = frameIndex % RenderFrameContextCount`.
2. Call the slot's existing `WaitAndReset`.
3. If `submittedFrameIndices[slotIndex] != 0`, ask `VirtualShadowManager` to consume that exact completed frame.
4. Publish the returned available or unavailable value.
5. Clear the stored submitted frame index before recording the new frame.

No call in this path may add `WaitIdle`, poll, or map before `WaitAndReset` succeeds.

- [ ] **Step 5: Commit/abort statistics with queue submission**

During `RenderMainSwapchainFrame`:

- After family recording identifies a scene, call `performanceStatistics.ActivateScene(sceneIdentity, frameIndex)` so a previous scene disappears immediately.
- After a successful `SubmitMainSwapchainFrame`, store `submittedFrameIndices[slotIndex] = frameIndex`.
- Commit the manager statistics record only for a successfully submitted frame.
- Abort the record on recording or submit failure.

Keep the existing `VirtualShadowPreparedFrameTicket` transaction independent: page-cache commit/abort and diagnostics commit/abort have different failure behavior.

- [ ] **Step 6: Expose the Main Thread getter**

Add to `RenderSystem`:

```cpp
[[nodiscard]] RenderPerformanceStatistics GetPerformanceStatistics() const;
```

It returns `impl_->performanceStatistics.GetLatest()` and never executes a synchronous render command.

Reset the exchange and submitted frame indices during device teardown so an old device/scene sample cannot survive reinitialization.

- [ ] **Step 7: Run focused tests and smoke**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineRenderPerformanceStatisticsTests VEngineVirtualShadowGpuPipelineSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R "VEngine(RenderPerformanceStatistics|VirtualShadow)" --output-on-failure
```

Expected: manager lifecycle, scene rejection, thread-safe publication, and both graphics backends pass.

- [ ] **Step 8: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp Engine/Runtime/Render/RenderSystem.h Engine/Runtime/Render/RenderSystem.cpp Tests/Unit/VirtualShadowTests.cpp Tests/Unit/RenderPerformanceStatisticsTests.cpp Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp
git commit -m "render: publish completed VSM statistics"
```

## Task 5: Render The Four-Region Editor Footer

**Files:**

- Modify: `Editor/Core/EditorProjectEditingView.cpp`
- Modify: `Tests/Unit/RenderPerformanceStatisticsTests.cpp`

- [ ] **Step 1: Extend the footer model test**

Assert:

- FPS is retained when VSM is unavailable.
- Available VSM values are copied exactly.
- Render and Physics remain explicit placeholder sections.
- The four column weights sum to `1.0F`.

- [ ] **Step 2: Run the focused test**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRenderPerformanceStatisticsTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineRenderPerformanceStatisticsTests --output-on-failure
```

Expected: the new placeholder/model assertions fail until the model is finalized.

- [ ] **Step 3: Replace the one-line status bar**

Use `EditorPerformanceFooterHeight` instead of the old 24-pixel constant. Build the model from:

```cpp
const Float32 framesPerSecond = editor.GetRuntime().GetTimeSystem().GetAverageFrameRate();
const RenderPerformanceStatistics statistics = editor.GetRenderSystem().GetPerformanceStatistics();
const EditorPerformanceFooterModel model = BuildEditorPerformanceFooterModel(framesPerSecond, statistics);
```

Draw one ImGui table:

```cpp
constexpr ImGuiTableFlags TableFlags =
    ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX;

if (ImGui::BeginTable("EditorPerformanceFooter", 4, TableFlags))
{
    ImGui::TableSetupColumn("Common", ImGuiTableColumnFlags_WidthStretch, CommonFooterColumnWeight);
    ImGui::TableSetupColumn("Render", ImGuiTableColumnFlags_WidthStretch, RenderFooterColumnWeight);
    ImGui::TableSetupColumn("VSM", ImGuiTableColumnFlags_WidthStretch, VirtualShadowFooterColumnWeight);
    ImGui::TableSetupColumn("Physics", ImGuiTableColumnFlags_WidthStretch, PhysicsFooterColumnWeight);
    // Draw one vertically stacked region per column.
    ImGui::EndTable();
}
```

Render subdued section titles and these rows:

```text
Common
FPS: 60.0

Render
—

VSM
Physical: 2048 total | 783 allocated
Frame: 412 requested | 367 cached | 45 redraw
Alloc: 12 new | 0 unmapped

Physics
—
```

Use `"\xE2\x80\x94"` for the UTF-8 em dash. When VSM is unavailable, draw a single em dash instead of stale or zero-valued counters.

- [ ] **Step 4: Build the Editor**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: Editor builds successfully.

- [ ] **Step 5: Run an Editor smoke check**

Launch deterministically:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Verify:

- Footer height is 74 pixels.
- Previous main content retains its visual height in the 850-pixel window.
- Four regions use approximately 18/18/46/18 percent widths.
- Vertical separators and stacked rows do not clip.
- FPS updates as before.
- VSM initially shows an em dash, then completed-frame values on D3D11 and D3D12.
- Render and Physics show em dashes.

- [ ] **Step 6: Commit**

```text
git add Editor/Core/EditorProjectEditingView.cpp Tests/Unit/RenderPerformanceStatisticsTests.cpp
git commit -m "editor: add performance footer regions"
```

## Task 6: Document And Verify The Complete Change

**Files:**

- Modify: `Docs/ArchitectureOverview.md`

- [ ] **Step 1: Update the architecture overview**

Document:

- Scene-shared VSM counter ownership.
- One readback buffer per in-flight frame slot.
- Fence-completed, normally delayed publication.
- Scene-token rejection of stale samples.
- Main Thread access only through `RenderSystem::GetPerformanceStatistics`.

Do not duplicate the complete metric table from the design spec; link ownership and thread boundaries to the existing render architecture.

- [ ] **Step 2: Review the diff for scope and user-owned changes**

Run:

```text
git diff --check
git status --short
git diff -- Editor/Windows/main.cpp Engine/Runtime/Render/FrameContext.h
```

Expected:

- No whitespace errors.
- `Editor/Windows/main.cpp` still contains only the user's height change and is not staged by this plan.
- `Engine/Runtime/Render/FrameContext.h` is not modified by this implementation.

- [ ] **Step 3: Run the full Windows test suite**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 4: Build all Debug application targets**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: `VEnginePlayer`, `VEngineWinEditor`, tools, and enabled backend targets build.

- [ ] **Step 5: Repeat the real VSM smoke test explicitly**

```text
Build/windows-msvc-tests/Debug/VEngineVirtualShadowGpuPipelineSmokeTests.exe
```

Expected: D3D11 and D3D12 counter copy/readback checks pass and the process exits with code 0.

- [ ] **Step 6: Request code review and address findings**

Use the `requesting-code-review` skill. Review specifically for:

- Reads occurring only after the matching frame-context fence.
- D3D12 buffer state correctness.
- D3D11 staging/map correctness and binding hazards.
- No stale scene publication.
- Atomic counter meanings matching the design contract.
- Readback failure remaining non-fatal.

Repeat focused verification after every fix.

- [ ] **Step 7: Commit the documentation**

```text
git add Docs/ArchitectureOverview.md
git commit -m "docs: describe render performance publication"
```

- [ ] **Step 8: Perform final verification**

Use the `verification-before-completion` skill and record the exact commands and passing results in the final handoff.
