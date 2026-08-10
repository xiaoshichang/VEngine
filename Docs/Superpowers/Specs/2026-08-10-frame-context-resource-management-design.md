# FrameContext Resource Management Refactor Design

## Purpose

Refactor render-resource lifetime and uniform-buffer ownership so `FrameContext` no longer acts as a generic per-frame resource-retention and uniform-allocation container.

The change removes these fields from `FrameContext`:

```cpp
std::pmr::vector<std::shared_ptr<rhi::RhiObject>> inFlightGpuFrameObjects_;
FrameUniformAllocator uniformAllocator_;
RenderFrameUniformCache uniformCache_;
```

Persistent GPU resources are owned by their Render Thread proxies. Each `FrameContext` owns the fence-aware transient pool and retirement queue for its reusable GPU submission slot. Replaced and deleted RHI objects are retired through `RenderSystem` and released only after every GPU submission that may reference them has completed.

The implementation is performed on `codex/frame-context-resource-management` in the current checkout. It does not use a worktree or subagents and does not add unit tests.

## Goals

- Remove the three `FrameContext` fields listed above.
- Remove the per-frame visible-resource scan, sort, and deduplication performed by `BaseRenderer::RetainInFlightGpuFrameObjects()`.
- Route Scene Thread resource creation, modification, replacement, and deletion through the render command queue.
- Keep all live RHI creation, mutation, replacement, and destruction on the Render Thread.
- Delay deletion according to actual submitted GPU fence points rather than a fixed CPU frame-number offset.
- Give frame-, scene-, view-, object-, material-, and pass-frequency uniforms explicit RT owners.
- Prevent CPU writes from overwriting uniform data still consumed by an in-flight GPU submission.
- Give FrameGraph transient resources, pass-local resources, and imported persistent resources the same RHI lifetime contract.

## Non-Goals

- Adding Unreal Engine's full RDG shader-parameter or uniform-buffer system.
- Replacing the existing RHI abstraction.
- Adding a general bindless resource registry.
- Adding unit tests or a new test framework.
- Handling recovery when RHI resource creation fails. Existing assertion and error behavior remains the implementation baseline.
- Changing the number of reusable frame contexts.
- Broad rendering or scene-model refactoring unrelated to resource lifetime.

## Design Principles

The design follows the ownership boundaries used by larger threaded renderers while keeping VEngine explicit and small:

```text
Persistent resource     -> long-lived RT owner
Transient resource      -> current FrameContext's resource pool
Replaced/dead resource  -> fence retirement queue
FrameGraph              -> describes and borrows resources; it does not extend persistent lifetime
Scene Thread            -> submits immutable change payloads; it never mutates live RHI state
```

Logical ownership and physical RHI ownership remain distinct. Scene Thread objects may hold `shared_ptr` references to RT proxies, but only Render Thread code creates, replaces, extracts, pools, or destroys the proxies' RHI objects.

## Common RHI Lifetime Contract

Every persistent RT owner follows the same resource lifecycle:

1. A Scene Thread change is copied into a render command.
2. The Render Thread creates or updates the replacement RHI state.
3. The RT owner swaps to the replacement state.
4. The old RHI objects are moved out of the owner.
5. `RenderSystem` records the already-submitted fence points that may still reference those objects.
6. The objects are destroyed after all recorded fence points complete.

RHI object references are transferred rather than collected every frame. Existing `AppendRhiObjects()` APIs used only for frame retention are replaced by explicit extraction APIs shaped like:

```cpp
using RhiObjectList = std::vector<std::shared_ptr<rhi::RhiObject>>;

[[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;
```

The concrete API may use typed helpers or append into a caller-owned output list when that avoids allocations, but its semantic requirement is ownership transfer: after extraction the previous RT owner no longer exposes the retired objects as live state.

The requested `AppendingDeleteRTResources` concept is represented by the `pendingDeleteRTResourceQueue_` owned by each `FrameContext`. This follows the repository's member naming style and keeps each queue next to the fence that governs it.

## Fence-Based Deferred Deletion

CPU frame number `N + RenderFrameContextCount` is useful for diagnostics but is not the deletion authority. Paused presentation, missing submissions, backend behavior, or a future context-count change can make a fixed frame offset unsafe.

Deletion uses the actual fence associated with each submitted `FrameContext` slot. The conceptual storage is:

```cpp
struct PendingDeleteRTResourceBatch
{
    RhiObjectList resources;
    UInt32 remainingFenceCount = 0;
};

struct PendingDeleteRTResourceEntry
{
    UInt64 fenceValue = 0;
    std::shared_ptr<PendingDeleteRTResourceBatch> batch;
};

class FrameContext
{
    FrameTransientResourcePool transientResourcePool_;
    std::deque<PendingDeleteRTResourceEntry> pendingDeleteRTResourceQueue_;
    UInt64 submittedFrameIndex_ = 0;
};
```

When an RT owner retires RHI objects, `RenderSystemImpl` snapshots the submitted fence value of every frame slot that can still reference them:

- A slot without an outstanding submission adds no dependency.
- Each outstanding slot receives one ordered queue entry referencing the shared retirement batch.
- If retirement occurs while a frame command list is being recorded, and that command list may already reference the object, the active slot's next submission fence value is recorded as a dependency as well.
- `remainingFenceCount` equals the number of recorded slot dependencies.
- A batch with no dependencies is released immediately on the Render Thread.

`RenderSystemImpl` therefore tracks whether a main-swapchain frame is actively recording, its frame-slot index, and the fence value that `FrameContext::Submit()` will signal. Submission failure remains fatal under the existing rendering policy, so the design does not add rollback for a retirement dependency targeting the active submission.

When a frame slot becomes reusable, `RenderSystem` asks that `FrameContext` to drain only its ordered queue entries whose target value is no greater than the completed value. Each drained entry decrements its batch dependency count. The last completed dependency releases the batch's RHI objects.

The completed target is captured before `FrameContext::WaitForFrameStartAndReset()` clears its submitted value. After the wait succeeds, `RenderSystem` uses that captured target to collect the slot queue. A frame slot has at most one outstanding submission because it is never reused before its previous fence completes.

Fence targets for each slot are monotonically nondecreasing, so collection consumes queue prefixes. It does not scan the scene, visible RenderItems, built-in shaders, imported render targets, or the full set of pending resources.

## Command Ordering

The Scene Thread uses one ordered render command stream:

```text
update or unregister RT consumers
  -> update or release the referenced RT resource
  -> enqueue subsequent RenderFrame work
```

After a resource-release command executes, later render commands must not record new GPU work referencing the released RHI state. A `shared_ptr` to an RT proxy only protects CPU object memory; it does not authorize a released proxy to remain renderable.

For Mesh, Material, Texture, Shader, RenderTexture, scene items, cameras, and views:

- Scene Thread changes are copied into immutable command payloads.
- RT consumers are updated or unregistered before a resource is released.
- Render Thread methods mutate the RT proxy and its RHI state.
- No Scene Thread destructor directly resets a live RHI object.

## FrameContext Responsibilities

After the refactor, `FrameContext` owns only state that is intrinsically tied to one reusable GPU submission slot:

- Command list.
- Completion fence.
- Submitted and next fence values.
- The slot's transient RHI resource pool.
- The slot's ordered pending-deletion queue.
- The frame index associated with the slot's outstanding submission, for completed statistics publication.
- True frame-global uniform data, if such data is required by the current pipeline.

`FrameContext` no longer owns a generic vector of arbitrary referenced RHI objects, a page allocator, or maps keyed by scenes, cameras, and render items.

The three slot-local members are singular. `RenderSystemImpl` continues to own `std::array<FrameContext, RenderFrameContextCount>`; a `FrameContext` does not contain another array indexed by frame slot. Cross-slot dependency discovery and the shared retirement batch remain `RenderSystem` responsibilities.

`PendingDeleteRTResourceBatch` and `PendingDeleteRTResourceEntry` are shared lifetime vocabulary and live in `RenderResourceLifetime.h`. The slot-local members remain private. `FrameContext` exposes narrow operations to initialize and shut down its transient pool, enqueue and collect retirement entries, clear retirement state after `WaitIdle()`, get the transient pool for frame-pipeline setup, and set or take the submitted frame index. `RenderSystem` does not receive a mutable deque reference.

A frame-slot resource may be updated only after `WaitForFrameStartAndReset()` or `WaitAndReset()` confirms that slot's prior submission is complete.

## Uniform Ownership

Uniform buffers use owner-local frame-slot versions. This is necessary because current D3D12 and Metal buffer updates write mapped CPU-visible memory directly and do not provide an engine-level automatic rename guarantee. D3D11 `Discard` behavior does not remove the cross-backend requirement.

A lightweight typed helper may be introduced:

```cpp
template<typename T>
class RTDynamicUniformBuffer
{
public:
    void Update(rhi::RhiDevice& device, UInt32 frameSlotIndex, const T& data, UInt64 revision);
    [[nodiscard]] rhi::RhiBuffer* GetBuffer(UInt32 frameSlotIndex) const noexcept;
    [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

private:
    std::array<std::shared_ptr<rhi::RhiBuffer>, RenderFrameContextCount> buffers_;
    std::array<UInt64, RenderFrameContextCount> uploadedRevisions_{};
};
```

This helper is an owner-local RHI wrapper, not an allocator. It creates one appropriately sized CPU-to-GPU uniform buffer per frame slot and uploads only when the slot's uploaded revision differs from the owner's current CPU data revision.

### Frame And Scene Frequency

Truly frame-global parameters belong to the current `FrameContext`, whose own fence makes its single slot-local buffer safe to update after frame preparation.

The existing `FrameUniformData` contains directional-light and ambient scene data, so it is scene-scoped rather than globally frame-scoped. It is renamed to `SceneUniformData` and owned by `RTScene` through a per-slot `RTDynamicUniformBuffer<SceneUniformData>`.

This preserves support for more than one scene without recreating a map inside `FrameContext`.

### View Frequency

View uniform buffers belong to `RTRenderViewState`.

- `GameViewPanel`, `SceneViewPanel`, and Player Application retain the Scene Thread-side view state at their existing lifetime level.
- `RenderViewState` owns or maps to `RTRenderViewState`.
- `RTRenderViewState` owns per-frame-slot view uniform buffers and uploaded revisions.
- Camera and target-extent changes update CPU snapshot data and revision through render commands.
- Rendering updates only the current safe slot when required.

The UI panel classes do not directly own backend RHI buffers.

### Object Frequency

Object uniform buffers belong to `RTRenderItem`.

- Transform, bounds, shadow flags, and other object changes update RT proxy data and revision.
- Rendering lazily uploads the current slot when its uploaded revision is stale.
- Removing the item extracts all slot buffers and retires them through `RenderSystem`.

### Material Frequency

Material uniforms belong to `RTMaterialResource`. The current `WaitForAllFrameContexts()` calls during material update and release are removed.

The material uniform storage must become frame-slot safe. The implementation may use per-slot material-pool allocations or per-material slot buffers, choosing the smaller change that preserves the existing `MaterialUniformPool` boundary. Reusing or releasing a material allocation is fence-gated through the same retirement mechanism.

### Pass Frequency

Pass constants currently uploaded through `FrameRenderPipelineData::UploadUniform()` are assigned to their long-lived RT owner:

- Virtual Shadow Map constants belong to `VirtualShadowManager` or its persistent scene/view cache.
- Scene grid constants and geometry belong to a persistent Editor render owner.
- Other persistent RenderPass resources use owner-local per-slot buffers.
- Truly frame-graph-transient constants come from a per-slot transient resource pool.

This closes the lifetime gap left by removing `FrameUniformAllocator`; pass-local constants do not fall back to ad hoc per-frame RHI creation and retention.

## FrameGraph Transient Resources

FrameGraph-created transient textures and buffers use the pool owned by the selected `FrameContext`. `RenderSystem` selects the slot and passes that pool to the frame pipeline; it does not maintain a parallel pool array.

For a selected slot:

1. Frame preparation waits for the slot fence.
2. The slot's prior transient resources become available for reuse.
3. FrameGraph acquires matching resources from that slot pool or creates new ones.
4. Recorded resources stay owned by the slot pool through submission.
5. They are not returned to another slot and are not exposed to Scene Thread code.

Normal per-frame reuse does not enqueue deletion. Pool trimming, incompatible replacement, device teardown, or pool shutdown extracts RHI objects and sends them through the common retirement path when GPU work is still outstanding.

The current FrameGraph calls to `RetainInFlightGpuFrameObject()` are removed.

## Persistent Pass Resources

SceneGrid, VSM, debug preview, and similar resources use the same RT owner contract as Mesh and RenderTexture:

- Long-lived pass or manager objects own their typed RHI objects.
- Frequently updated data uses per-slot buffers.
- Rebuild and destruction extract the old RHI objects.
- Extracted objects enter the common fence retirement path.
- A pass object whose C++ lifetime is only one FrameGraph execution must borrow from the transient pool rather than own an untracked RHI object.

The SceneGrid path no longer moves its vertex buffer into `FrameContext` after recording.

## Imported Persistent Resources

FrameGraph imports typed non-owning pointers to persistent resources for command recording. The authoritative reference remains with the RT owner:

- `RTRenderTexture` owns imported color and depth textures.
- `RTMeshResource` owns vertex and index buffers.
- `RTShaderResource` owns shader-stage RHI objects.
- `RTMaterialResource` owns material uniform state and references its shader proxy.
- Built-in shader libraries own their RT shader proxies.

FrameGraph does not retain a new `shared_ptr` every frame. Command ordering prevents new use after release, and fence retirement prevents physical destruction while older submissions remain in flight.

## Per-Frame Cost

The current cost includes traversal of opaque and transparent render queues, repeated discovery across views, built-in shader collection, imported target retention, sort, unique, and insertion into `FrameContext`.

After the refactor:

```text
normal frame:
  update stale owner-local uniform slots
  acquire/reuse frame-slot transient resources
  drain the completed retirement queue prefix for the reused slot

resource change:
  extract only the changed resource's old RHI objects
  append fence dependencies only for outstanding slots
```

The work becomes proportional to changed resources, stale uniform owners actually rendered, transient resources actually requested, and retirement entries that complete. There is no generic scan of every live or visible RHI object for lifetime purposes.

## Shutdown

Shutdown uses this order on the Render Thread:

```text
stop accepting new frame work
  -> drain accepted render commands
  -> wait for the RHI device to become idle
  -> clear each FrameContext's transient pool and pending deletion queue
  -> release remaining RT-owner RHI objects
  -> destroy FrameContext command lists and fences
  -> destroy the RHI device
```

After `WaitIdle()`, all outstanding fence dependencies are satisfied, so shutdown clears pools and retirement queues immediately rather than reenqueuing their resources.

## Code Removal And Migration

The completed refactor removes:

- `FrameContext::RetainInFlightGpuFrameObject()`.
- `FrameRenderPipelineData::RetainInFlightGpuFrameObject()`.
- `BaseRenderer::RetainInFlightGpuFrameObjects()`.
- The `FrameUniformAllocator` implementation and build entries.
- The `RenderFrameUniformCache` implementation and build entries.
- Per-frame `AppendRhiObjects()` calls used only for retention.
- Material-update and material-release waits across all FrameContexts.

Call sites of `UploadUniform()`, `GetFrameUniform()`, `GetViewUniform()`, and `GetObjectUniform()` migrate to their explicit RT owners.

## Verification

No new unit tests are added. Verification uses existing project build, CTest, static inspection, and rendering smoke coverage.

Windows commands run through the required MSVC wrapper:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Editor smoke testing launches the project directly:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Acceptance checks:

- D3D11 and D3D12 builds complete.
- Existing CTest tests pass without modification.
- Editor Scene View and Game View render correctly while using independent view uniform ownership.
- Player rendering continues to bind valid scene, view, object, material, and pass constants.
- Repeated transform and camera changes update the expected current frame slot without waiting for all contexts.
- Mesh, Material, Texture, Shader, RenderTexture, and pass-resource replacement does not destroy RHI state before its recorded fence dependencies complete.
- FrameGraph transient resources are reused only after their owning frame slot completes.
- Material updates and releases no longer wait for every FrameContext.
- Shutdown leaves no pending retirement entries or transient-pool resources before device destruction.
- Static search of engine, editor, player, test, and build-manifest sources finds no remaining implementation references to:

```text
inFlightGpuFrameObjects_
FrameUniformAllocator
RenderFrameUniformCache
RetainInFlightGpuFrameObject
BaseRenderer::RetainInFlightGpuFrameObjects
```
