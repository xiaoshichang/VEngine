# FrameContext Resource Management Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. The user explicitly prohibited subagents. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove generic RHI retention and uniform allocation from `FrameContext`, replace them with RT-owner resources, frame-slot transient pools, and fence-driven deferred deletion.

**Architecture:** Persistent Scene, View, Object, Material, Mesh, Texture, Shader, and RenderTexture RHI objects remain with their Render Thread owners. RenderSystem owns one transient resource pool per reusable frame slot and one ordered retirement queue per slot; release batches wait for every submitted or actively recording fence that may reference them. FrameGraph borrows imported resources and acquires graph-owned resources from the current slot pool.

**Tech Stack:** C++20, CMake, VEngine common RHI, Render Thread command queue, per-FrameContext fences, Win32/MSVC, D3D11, D3D12.

**Execution constraints:** Work in the current checkout on `codex/frame-context-resource-management`. Do not create a worktree, do not dispatch subagents, do not add unit tests, and do not add resource-creation rollback behavior.

---

## File Structure

New focused files:

- `Engine/Runtime/Render/RenderFrameConfig.h`: reusable frame-slot count shared without including `FrameContext.h`.
- `Engine/Runtime/Render/RenderResourceLifetime.h`: `RhiObjectList` and move/extraction helpers shared by RT owners and RenderSystem.
- `Engine/Runtime/Render/RenderUniformBuffer.h`: uniform data structures, `UniformBufferAllocation`, and owner-local per-slot buffer declarations.
- `Engine/Runtime/Render/RenderUniformBuffer.cpp`: uniform-data builders and per-slot buffer creation/update/extraction.
- `Engine/Runtime/Render/FrameTransientResourcePool.h`: RenderSystem-owned texture, uniform-buffer, and adopted-object pool for one safe frame slot.
- `Engine/Runtime/Render/FrameTransientResourcePool.cpp`: pool acquisition, per-frame reset, reuse, and extraction.
- `Engine/Runtime/Render/RenderFramePipelineData.cpp`: frame-data accessors that route uniform requests to RT owners or the transient pool.

Deleted files after call-site migration:

- `Engine/Runtime/Render/FrameUniformAllocator.h`
- `Engine/Runtime/Render/FrameUniformAllocator.cpp`
- `Engine/Runtime/Render/RenderFrameUniformCache.h`
- `Engine/Runtime/Render/RenderFrameUniformCache.cpp`
- `Engine/Runtime/Render/MaterialUniformPool.h`
- `Engine/Runtime/Render/MaterialUniformPool.cpp`

Primary modified owners and orchestration:

- `Engine/Runtime/Render/FrameContext.h/.cpp`
- `Engine/Runtime/Render/RenderSystem.h/.cpp`
- `Engine/Runtime/Render/RenderFramePipelineData.h`
- `Engine/Runtime/Render/RenderResource.h/.cpp`
- `Engine/Runtime/Render/RenderTexture.h/.cpp`
- `Engine/Runtime/Render/RenderScene.h/.cpp`
- `Engine/Runtime/Render/RenderViewState.h/.cpp`
- `Engine/Runtime/Scene/Scene.h/.cpp`
- `Engine/Runtime/Scene/SceneSystem.h/.cpp`

Primary modified render consumers:

- `Engine/Runtime/Render/Renderer/BaseRenderer.h/.cpp`
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h/.cpp`
- `Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.cpp`
- `Engine/Runtime/Render/Renderer/RenderPass/OpaqueForwardPass.cpp`
- `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- `Engine/Runtime/Render/Renderer/RenderPass/ShadowCasterDirtyDebugPass.cpp`
- `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowRedrawPageDebugPass.cpp`
- `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h/.cpp`
- `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h/.cpp`
- `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep7_RenderCasters.cpp`
- `Editor/RenderPass/SceneGridRenderPass.h/.cpp`
- `Editor/RenderPass/EditorGizmoRenderPass.h/.cpp`
- `Engine/Render/PBR/HdrToneMappingPass.cpp`

Owner-construction changes:

- `Editor/Panels/SceneViewPanel/SceneViewPanel.cpp`
- `Editor/Panels/GameViewPanel/GameViewPanel.cpp`
- `Engine/Runtime/Scene/MeshRenderComponent.cpp`

Build manifest:

- `CMake/Targets/Engine.cmake`

---

### Task 1: Establish the clean baseline and add common frame-resource primitives

**Files:**
- Create: `Engine/Runtime/Render/RenderFrameConfig.h`
- Create: `Engine/Runtime/Render/RenderResourceLifetime.h`
- Create: `Engine/Runtime/Render/RenderUniformBuffer.h`
- Create: `Engine/Runtime/Render/RenderUniformBuffer.cpp`
- Create: `Engine/Runtime/Render/FrameTransientResourcePool.h`
- Create: `Engine/Runtime/Render/FrameTransientResourcePool.cpp`
- Modify: `Engine/Runtime/Render/FrameContext.h`
- Modify: `CMake/Targets/Engine.cmake`
- Test: existing CTest targets only

- [x] **Step 1: Verify the branch starts from a passing baseline**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: configure and build exit `0`; CTest reports `100% tests passed`.

- [x] **Step 2: Move the frame-slot count to a dependency-neutral header**

Create `RenderFrameConfig.h` with:

```cpp
#pragma once

#include "Engine/Runtime/Core/Types.h"

namespace ve
{
    inline constexpr UInt32 RenderFrameContextCount = 2;
} // namespace ve
```

Include it from `FrameContext.h`, `EditorGizmoRenderPass.h`, `VirtualShadowManager.cpp`, and every new per-slot helper. Remove the constant definition from `FrameContext.h`.

- [x] **Step 3: Add transferable RHI ownership vocabulary**

Create `RenderResourceLifetime.h` with:

```cpp
#pragma once

#include "Engine/RHI/Common/RhiDevice.h"

#include <memory>
#include <utility>
#include <vector>

namespace ve
{
    using RhiObjectList = std::vector<std::shared_ptr<rhi::RhiObject>>;

    template<typename TObject>
    void MoveRhiObject(RhiObjectList& objects, std::shared_ptr<TObject>& object)
    {
        if (object != nullptr)
        {
            objects.emplace_back(std::move(object));
        }
    }
} // namespace ve
```

Use shared ownership only at the retirement boundary; typed RT owners continue exposing raw pointers for binding.

- [x] **Step 4: Add owner-local per-slot uniform buffers**

Define in `RenderUniformBuffer.h`:

```cpp
struct UniformBufferAllocation
{
    rhi::RhiBuffer* buffer = nullptr;
    UInt64 offset = 0;
    UInt64 size = 0;
};

class RTDynamicUniformBuffer final : public NonCopyable
{
public:
    [[nodiscard]] UniformBufferAllocation GetOrUpdate(rhi::RhiDevice& device,
                                                      UInt32 frameSlotIndex,
                                                      const void* data,
                                                      UInt64 size,
                                                      UInt64 revision,
                                                      const char* debugName);
    [[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;

private:
    std::array<std::shared_ptr<rhi::RhiBuffer>, RenderFrameContextCount> buffers_;
    std::array<UInt64, RenderFrameContextCount> uploadedRevisions_{};
};
```

Move `FrameUniformData`, `ViewUniformData`, and `ObjectUniformData` from `RenderFrameUniformCache.h`; rename `FrameUniformData` to `SceneUniformData`. Expose these builders:

```cpp
[[nodiscard]] SceneUniformData BuildSceneUniformData(const RTScene& scene) noexcept;
[[nodiscard]] ViewUniformData BuildViewUniformData(const RTCamera* camera, rhi::RhiExtent2D targetExtent) noexcept;
[[nodiscard]] ObjectUniformData BuildObjectUniformData(const RTRenderItem& item) noexcept;
```

`GetOrUpdate` creates a `CpuToGpu` uniform buffer when the selected slot has none, updates with `Discard` only when the slot revision differs, and returns offset zero. If the slot size changes, replace that slot directly because its fence has completed before access.

- [x] **Step 5: Add the frame-slot transient pool**

Define `FrameTransientResourcePool` with these public operations:

```cpp
void Initialize(rhi::RhiDevice& device) noexcept;
void BeginFrame() noexcept;
[[nodiscard]] std::shared_ptr<rhi::RhiTexture> AcquireTexture(const rhi::RhiTextureDesc& desc);
void ReleaseTexture(const rhi::RhiTextureDesc& desc, std::shared_ptr<rhi::RhiTexture> texture);
[[nodiscard]] UniformBufferAllocation UploadUniform(const void* data, UInt64 size, const char* debugName);
void Adopt(std::shared_ptr<rhi::RhiObject> object);
[[nodiscard]] RhiObjectList Shutdown() noexcept;
```

Define an internal `TransientTextureKey` from the RHI descriptor's dimension, width, height, depth, mip count, format, and usage; exclude `debugName` and initial-data pointers. Use that key for texture free lists so the pool remains RHI-oriented rather than depending on FrameGraph types. Use whole `CpuToGpu` RHI uniform buffers keyed by aligned size; a buffer uploaded in the current frame remains in `usedUniformBuffers_` until the next `BeginFrame()`. `Adopt` holds short-lived samplers, vertex buffers, and other pass objects until the slot becomes reusable. Do not suballocate uniform pages.

- [x] **Step 6: Register the new files and compile the primitives**

Add all new `.h` and `.cpp` files to `CMake/Targets/Engine.cmake`, then run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: build exits `0`; existing behavior is unchanged because no call site uses the new helpers yet.

- [x] **Step 7: Commit the common primitives**

```text
git add CMake/Targets/Engine.cmake Engine/Runtime/Render/RenderFrameConfig.h Engine/Runtime/Render/RenderResourceLifetime.h Engine/Runtime/Render/RenderUniformBuffer.h Engine/Runtime/Render/RenderUniformBuffer.cpp Engine/Runtime/Render/FrameTransientResourcePool.h Engine/Runtime/Render/FrameTransientResourcePool.cpp Engine/Runtime/Render/FrameContext.h Editor/RenderPass/EditorGizmoRenderPass.h Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp
git commit -m "render: add frame-slot resource primitives"
```

### Task 2: Add RenderSystem fence retirement and frame-slot pool lifecycle

**Files:**
- Modify: `Engine/Runtime/Render/RenderSystem.h`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Modify: `Engine/Runtime/Render/FrameContext.h`
- Modify: `Engine/Runtime/Render/FrameContext.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipelineData.h`
- Create: `Engine/Runtime/Render/RenderFramePipelineData.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Test: existing CTest targets only

- [x] **Step 1: Add one transient pool and one retirement queue per frame slot**

Add private RenderSystem implementation types:

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
```

Add to `RenderSystemImpl`:

```cpp
std::array<FrameTransientResourcePool, RenderFrameContextCount> transientResourcePools;
std::array<std::deque<PendingDeleteRTResourceEntry>, RenderFrameContextCount> pendingDeleteRTResourceQueues;
bool recordingFrame = false;
UInt32 recordingFrameSlotIndex = 0;
UInt64 recordingSubmissionFenceValue = 0;
```

- [x] **Step 2: Implement retirement without scanning live resources**

Add private functions in `RenderSystem.cpp`:

```cpp
void RetireRhiObjects(RenderSystemImpl& impl, RhiObjectList objects);
void CollectRetiredRhiObjects(RenderSystemImpl& impl, UInt32 frameSlotIndex, UInt64 completedFenceValue);
void ClearRetiredRhiObjectsAfterWaitIdle(RenderSystemImpl& impl) noexcept;
```

`RetireRhiObjects` snapshots every nonzero `FrameContext::GetSubmittedFenceValue()`. When `recordingFrame` is true, also add the active slot's `recordingSubmissionFenceValue` unless that exact slot/value dependency is already present. Queue entries remain ordered by fence value. A zero-dependency batch dies immediately on the Render Thread.

`CollectRetiredRhiObjects` drains only the selected queue prefix with `entry.fenceValue <= completedFenceValue` and decrements `remainingFenceCount`.

- [x] **Step 3: Integrate pool reset and retirement collection into frame preparation**

Before `WaitForFrameStartAndReset`, capture:

```cpp
const UInt64 completedFenceTarget = frameContext.GetSubmittedFenceValue();
```

After the wait succeeds:

```cpp
CollectRetiredRhiObjects(impl, frameSlotIndex, completedFenceTarget);
impl.transientResourcePools[frameSlotIndex].BeginFrame();
```

Populate `FrameRenderPipelineData` with `frameSlotIndex` and a pointer to the selected pool.

- [x] **Step 4: Track the actively recording submission fence**

Immediately before `framePipeline->RenderFrame(frameData)` set:

```cpp
impl.recordingFrame = true;
impl.recordingFrameSlotIndex = frameData.frameSlotIndex;
impl.recordingSubmissionFenceValue = frameData.frameContext->GetNextSubmissionFenceValue();
```

After successful submit clear all three values. Existing fatal frame-error handling remains unchanged, so no rollback path is added.

- [x] **Step 5: Initialize and shut down transient pools with the RHI device**

Initialize each pool after frame contexts are created. During normal device teardown:

```text
device.WaitIdle()
transient pools Shutdown()
pending retirement queues clear
remaining RT services shut down
frame contexts shut down
device reset
```

After `WaitIdle`, destroy extracted pool objects immediately on the Render Thread.

- [x] **Step 6: Reduce FrameContext to submission-slot state**

Remove the retention allocator constructor parameter and make `FrameContext()` default. Do not remove the three target fields yet; call sites still require them until Tasks 5-8. Move `FrameRenderPipelineData` method definitions out of `FrameContext.cpp` into the new `RenderFramePipelineData.cpp` so the final `FrameContext.cpp` contains only frame-slot behavior.

- [x] **Step 7: Build and commit the lifecycle infrastructure**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: build exits `0`.

Commit:

```text
git add CMake/Targets/Engine.cmake Engine/Runtime/Render/RenderSystem.h Engine/Runtime/Render/RenderSystem.cpp Engine/Runtime/Render/FrameContext.h Engine/Runtime/Render/FrameContext.cpp Engine/Runtime/Render/RenderFramePipelineData.h Engine/Runtime/Render/RenderFramePipelineData.cpp
git commit -m "render: add fence-driven resource retirement"
```

### Task 3: Move persistent resource replacement and deletion to retirement batches

**Files:**
- Modify: `Engine/Runtime/Render/RenderResource.h`
- Modify: `Engine/Runtime/Render/RenderResource.cpp`
- Modify: `Engine/Runtime/Render/RenderTexture.h`
- Modify: `Engine/Runtime/Render/RenderTexture.cpp`
- Modify: `Engine/Runtime/Render/RenderSystem.h`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Modify: `Engine/Runtime/Resource/ResourceObject.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Delete later in this task: `Engine/Runtime/Render/MaterialUniformPool.h/.cpp`
- Test: existing CTest targets only

- [x] **Step 1: Replace reference-copy APIs with ownership extraction**

Remove `AppendRhiObjects()` from Mesh, Texture, and Shader proxies. Add `TakeRhiObjects()` to:

```cpp
RTMeshResource
RTTextureResource
RTShaderPass
RTShaderResource
RTMaterialResource
RTRenderTexture
```

Each method moves every owned `shared_ptr<rhi::RhiObject>` into an `RhiObjectList`, clears raw/native handles, and leaves the owner uninitialized or ready for rebuilding.

- [x] **Step 2: Retire old objects during init and replacement commands**

For Mesh, Texture, Shader, and RenderTexture init commands, use this order:

```cpp
RhiObjectList retiredObjects = proxy->TakeRhiObjects();
proxy->InitRenderResource(*impl_->device, std::move(desc));
RetireRhiObjects(*impl_, std::move(retiredObjects));
```

For RenderTexture, first preserve the current descriptor-match fast path. Only extract and recreate color/depth textures when extent, format, or depth requirements change.

- [x] **Step 3: Replace MaterialUniformPool with material-owned per-slot buffers**

Change `RTMaterialResource::InitRenderResource` to store the immutable descriptor and revision only. Add:

```cpp
[[nodiscard]] UniformBufferAllocation GetUniformBuffer(rhi::RhiDevice& device, UInt32 frameSlotIndex);
[[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;
```

Implement the getter with `RTDynamicUniformBuffer::GetOrUpdate`, using `desc_.constantData`, `desc_.revision`, and debug name `RTMaterialResourceUniform`. Empty constant data returns an empty allocation. Remove `MaterialUniformPool` from `RenderSystemImpl`, CMake, includes, initialization, shutdown, and release paths.

- [x] **Step 4: Retire persistent resources on explicit release commands**

Update all existing `RenderSystem::ReleaseRenderResource` overloads to execute:

```cpp
RetireRhiObjects(*impl_, proxy->TakeRhiObjects());
```

Remove both material `WaitForAllFrameContexts()` calls. Keep `WaitForAllFrameContexts()` only where device/swapchain lifecycle still requires it.

- [x] **Step 5: Give RenderTexture an explicit release path**

Make `RenderTexture` noncopyable, store the `RenderSystem*` used by `InitRenderResource`, and enqueue `ReleaseRenderResource(rtRenderTexture_)` from its destructor while that RenderSystem is initialized. Add the `RTRenderTexture` release overload to `RenderSystem.h/.cpp`. This makes panel resize and Player target replacement use the same retirement mechanism as Mesh and Texture resources.

- [x] **Step 6: Build, verify waits are gone, and commit**

Run:

```text
rg -n "WaitForAllFrameContexts" Engine/Runtime/Render/RenderSystem.cpp
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: `WaitForAllFrameContexts` remains only in RHI lifecycle paths; build exits `0`.

Commit:

```text
git add CMake/Targets/Engine.cmake Engine/Runtime/Render/RenderResource.h Engine/Runtime/Render/RenderResource.cpp Engine/Runtime/Render/RenderTexture.h Engine/Runtime/Render/RenderTexture.cpp Engine/Runtime/Render/RenderSystem.h Engine/Runtime/Render/RenderSystem.cpp Engine/Runtime/Resource/ResourceObject.cpp Engine/Runtime/Render/MaterialUniformPool.h Engine/Runtime/Render/MaterialUniformPool.cpp
git commit -m "render: retire persistent resources by fence"
```

### Task 4: Assign Scene, View, and Object uniforms to their RT owners

**Files:**
- Modify: `Engine/Runtime/Render/RenderScene.h`
- Modify: `Engine/Runtime/Render/RenderScene.cpp`
- Modify: `Engine/Runtime/Render/RenderViewState.h`
- Modify: `Engine/Runtime/Render/RenderViewState.cpp`
- Modify: `Engine/Runtime/Render/RenderSystem.h`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Modify: `Engine/Runtime/Scene/Scene.h`
- Modify: `Engine/Runtime/Scene/Scene.cpp`
- Modify: `Engine/Runtime/Scene/SceneSystem.h`
- Modify: `Engine/Runtime/Scene/SceneSystem.cpp`
- Modify: `Engine/Runtime/Scene/MeshRenderComponent.cpp`
- Modify: `Editor/Panels/SceneViewPanel/SceneViewPanel.cpp`
- Modify: `Editor/Panels/GameViewPanel/GameViewPanel.cpp`
- Test: existing CTest targets only

- [x] **Step 1: Add Scene and Object owner-local uniform getters**

Add to `RTScene`:

```cpp
[[nodiscard]] UniformBufferAllocation GetSceneUniform(rhi::RhiDevice& device, UInt32 frameSlotIndex, UInt64 frameIndex);
[[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;
```

Build `SceneUniformData` from the scene and use `frameIndex` as its upload revision so all passes and views in one frame share one upload.

Add to `RTRenderItem`:

```cpp
[[nodiscard]] UniformBufferAllocation GetObjectUniform(rhi::RhiDevice& device, UInt32 frameSlotIndex);
[[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;
```

Use `revision_` as the object uniform revision. Ensure `ApplyUpdateParam` increments or assigns a monotonic uniform revision whenever Transform or Shadows changes.

- [x] **Step 2: Add View owner-local uniform getters**

Add to `RTRenderViewState`:

```cpp
[[nodiscard]] UniformBufferAllocation GetViewUniform(rhi::RhiDevice& device,
                                                     UInt32 frameSlotIndex,
                                                     UInt64 frameIndex,
                                                     const RTCamera* camera,
                                                     rhi::RhiExtent2D targetExtent);
[[nodiscard]] RhiObjectList TakeRhiObjects() noexcept;
```

Use `frameIndex` as the upload revision. Assert that a single `RTRenderViewState` is not requested with two different camera/extent values in the same frame.

- [x] **Step 3: Add RenderSystem release commands for Scene, View, and Object owners**

Add overloads for `std::shared_ptr<RTScene>`, `std::shared_ptr<RTRenderViewState>`, and `std::shared_ptr<RTRenderItem>`. Each command calls `TakeRhiObjects()` and `RetireRhiObjects`.

- [x] **Step 4: Make Scene Thread owners enqueue those release commands**

Add `SceneSystem::ReleaseRenderResource` forwarding methods for Scene and RenderItem. In `Scene::~Scene`, enqueue scene uniform release after queued scene clear/removal commands. In `MeshRenderComponent::~MeshRenderComponent`, unregister the item first, then enqueue its uniform release; disabling and re-enabling a component must not release its uniform resource.

Change `RenderViewState` construction to:

```cpp
RenderViewState(RenderSystem& renderSystem, RenderViewStateDesc desc);
```

Store `RenderSystem*` and enqueue its RT view-state release in the destructor.

- [x] **Step 5: Update every RenderViewState construction site**

Create Editor Scene and Game view states during each panel's `Init(Editor&)`, using `editor.GetRenderSystem()`. Construct the Player state in `SceneSystem::Initialize` with the provided `RenderSystem&`. Preserve the existing view names `EditorSceneView`, `EditorGameView`, and `PlayerView`.

- [x] **Step 6: Build and commit owner-local uniforms**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: build exits `0`.

Commit:

```text
git add Engine/Runtime/Render/RenderScene.h Engine/Runtime/Render/RenderScene.cpp Engine/Runtime/Render/RenderViewState.h Engine/Runtime/Render/RenderViewState.cpp Engine/Runtime/Render/RenderSystem.h Engine/Runtime/Render/RenderSystem.cpp Engine/Runtime/Scene/Scene.h Engine/Runtime/Scene/Scene.cpp Engine/Runtime/Scene/SceneSystem.h Engine/Runtime/Scene/SceneSystem.cpp Engine/Runtime/Scene/MeshRenderComponent.cpp Editor/Panels/SceneViewPanel/SceneViewPanel.cpp Editor/Panels/GameViewPanel/GameViewPanel.cpp
git commit -m "render: move uniforms to render-thread owners"
```

### Task 5: Route render-pass uniform access through RT owners

**Files:**
- Modify: `Engine/Runtime/Render/RenderFramePipelineData.h`
- Modify: `Engine/Runtime/Render/RenderFramePipelineData.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueForwardPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/ShadowCasterDirtyDebugPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowRedrawPageDebugPass.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep7_RenderCasters.cpp`
- Modify: `Editor/RenderPass/SceneGridRenderPass.cpp`
- Modify: `Editor/RenderPass/EditorGizmoRenderPass.cpp`
- Test: existing CTest targets only

- [x] **Step 1: Replace cache-shaped frame-data APIs**

Expose these methods from `FrameRenderPipelineData`:

```cpp
[[nodiscard]] UniformBufferAllocation GetSceneUniform(RTScene& scene) const;
[[nodiscard]] UniformBufferAllocation GetViewUniform(RTRenderViewState& viewState,
                                                     const RTCamera* camera,
                                                     rhi::RhiExtent2D targetExtent) const;
[[nodiscard]] UniformBufferAllocation GetObjectUniform(RTRenderItem& item) const;
[[nodiscard]] UniformBufferAllocation GetMaterialUniform(RTMaterialResource& material) const;
[[nodiscard]] UniformBufferAllocation UploadTransientUniform(const void* data, UInt64 size, const char* debugName) const;
void AdoptTransientRhiObject(std::shared_ptr<rhi::RhiObject> object) const;
```

Each persistent getter delegates to the owner with `device`, `frameSlotIndex`, and `frameIndex`. Transient methods delegate to the selected `FrameTransientResourcePool`.

- [x] **Step 2: Migrate Scene and View bindings**

Replace every `GetFrameUniform(*scene)` with `GetSceneUniform(*scene)`. Replace every camera-only `GetViewUniform` call with:

```cpp
context.frameData.GetViewUniform(*viewData.view.viewState,
                                 viewData.view.camera.get(),
                                 rhi::RhiExtent2D{renderArea.width, renderArea.height});
```

Apply this exact ownership path in Depth, Opaque, Transparent, ShadowCasterDirtyDebug, VirtualShadowRedrawPageDebug, SceneGrid, and EditorGizmo passes.

- [x] **Step 3: Migrate Object and Material bindings**

Keep `GetObjectUniform(*item)` call shape but route it to the item owner. In Opaque and Transparent material binding functions, obtain the material allocation with `GetMaterialUniform(*materialResource)` instead of reading a global pool allocation directly.

Update ShadowCaster and VSM caster paths to use the same object allocation.

- [x] **Step 4: Build and commit the persistent uniform call sites**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: build exits `0`; `GetFrameUniform` no longer appears outside obsolete files.

Commit:

```text
git add Engine/Runtime/Render/RenderFramePipelineData.h Engine/Runtime/Render/RenderFramePipelineData.cpp Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.cpp Engine/Runtime/Render/Renderer/RenderPass/OpaqueForwardPass.cpp Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp Engine/Runtime/Render/Renderer/RenderPass/ShadowCasterDirtyDebugPass.cpp Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowRedrawPageDebugPass.cpp Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep7_RenderCasters.cpp Editor/RenderPass/SceneGridRenderPass.cpp Editor/RenderPass/EditorGizmoRenderPass.cpp
git commit -m "render: bind uniforms from render-thread owners"
```

### Task 6: Move pass-frequency constants and short-lived objects to the transient pool

**Files:**
- Modify: `Engine/Render/PBR/HdrToneMappingPass.cpp`
- Modify: `Editor/RenderPass/SceneGridRenderPass.h`
- Modify: `Editor/RenderPass/SceneGridRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueForwardPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/ShadowCasterDirtyDebugPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowRedrawPageDebugPass.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.cpp`
- Modify: all `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep*.cpp` files that call `UploadVirtualShadowPassConstants`
- Test: existing CTest targets only

- [x] **Step 1: Replace generic frame uploads with named transient uploads**

Change HDR settings, SceneGrid constants, debug values, forward VSM sampling constants, and all VSM compute-step constants to call `UploadTransientUniform(data, size, debugName)`. Use stable names:

```text
HdrToneMappingUniform
SceneGridUniform
ShadowCasterDirtyDebugUniform
VirtualShadowSamplingUniform
VirtualShadowPassUniform
```

Keep `UniformBufferAllocation` binding semantics unchanged: each transient allocation is one whole buffer at offset zero.

- [x] **Step 2: Move SceneGrid's temporary vertex buffer to the current slot pool**

After recording the grid draw, convert the created vertex buffer to shared ownership and call:

```cpp
context.frameData.AdoptTransientRhiObject(std::shared_ptr<rhi::RhiObject>(std::move(vertexBuffer_)));
```

This preserves the pass's current frame-local construction while moving lifetime out of `FrameContext`.

- [x] **Step 3: Move HDR's frame-local sampler to the current slot pool**

After the last draw recorded by each `HdrToneMappingDrawResources`, extract its sampler and adopt it through `FrameRenderPipelineData`. Do not let the per-frame pipeline destructor release a sampler referenced by the submitted command list.

- [x] **Step 4: Build, scan transient calls, and commit**

Run:

```text
rg -n "UploadUniform\(" Engine Editor Player
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: only obsolete allocator/cache definitions still contain `UploadUniform`; build exits `0`.

Commit:

```text
git add Engine/Render/PBR/HdrToneMappingPass.cpp Editor/RenderPass/SceneGridRenderPass.h Editor/RenderPass/SceneGridRenderPass.cpp Engine/Runtime/Render/Renderer/RenderPass/OpaqueForwardPass.cpp Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp Engine/Runtime/Render/Renderer/RenderPass/ShadowCasterDirtyDebugPass.cpp Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowRedrawPageDebugPass.cpp Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.h Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowPassCommon.cpp Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowStep*.cpp
git commit -m "render: pool pass-frequency frame resources"
```

### Task 7: Pool FrameGraph transients and remove imported-resource retention

**Files:**
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.cpp`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Render/PBR/HdrToneMappingPass.cpp`
- Test: existing CTest targets only

- [x] **Step 1: Make FrameGraph acquire and release transient textures through the selected slot pool**

Remove `Impl::availableTransientTextures`. `AcquirePassTextures` converts the graph descriptor with `BuildRhiTextureDesc` and calls `frameData.transientResourcePool->AcquireTexture`; `ReleasePassTextures` and `ReleaseAllTextures` return textures with the same normalized RHI descriptor through `ReleaseTexture`. Delete `RetainReleasedTextures` and all calls to it.

Preserve within-frame aliasing: a texture released after its last compiled pass is immediately eligible for a later nonoverlapping pass in the same command list.

- [x] **Step 2: Remove BaseRenderer's resource discovery scan**

Delete:

```cpp
BaseRenderer::RetainInFlightGpuFrameObjects()
```

Remove its call from `Render()`, its declaration from `BaseRenderer.h`, the temporary collection vector, built-in shader traversal, sort, and unique logic. Remove per-frame retention from imported color/depth target setup.

- [x] **Step 3: Treat imported resources as borrowed RT-owner state**

Remove the HDR destination retain call. Keep the frame pipeline's RT proxy references until recording completes, while physical replacement and deletion use the RenderSystem fence retirement queues established in Task 3.

- [x] **Step 4: Retire FrameGraph debug previews through RenderSystem**

Change `FrameGraphDebugPreviewTexture::Reset()` to return the extracted texture as an `RhiObjectList` or typed shared pointer. Pass `RenderSystemImpl&` into `RetireFrameGraphDebugDataOnRenderThread`, collect every preview texture, and call `RetireRhiObjects` rather than resetting the last RHI reference immediately.

Remove `FrameGraph.cpp`'s per-frame preview texture retention call.

- [x] **Step 5: Build and commit the scan removal**

Run:

```text
rg -n "RetainInFlightGpuFrameObject|RetainInFlightGpuFrameObjects|AppendRhiObjects" Engine Editor Player
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: matches remain only in `FrameContext` and obsolete files awaiting Task 8; build exits `0`.

Commit:

```text
git add Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.cpp Engine/Runtime/Render/RenderSystem.cpp Engine/Runtime/Render/Renderer/BaseRenderer.h Engine/Runtime/Render/Renderer/BaseRenderer.cpp Engine/Render/PBR/HdrToneMappingPass.cpp
git commit -m "render: remove per-frame resource retention scans"
```

### Task 8: Remove obsolete FrameContext resource managers

**Files:**
- Modify: `Engine/Runtime/Render/FrameContext.h`
- Modify: `Engine/Runtime/Render/FrameContext.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipelineData.h`
- Modify: `Engine/Runtime/Render/RenderFramePipelineData.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/FrameGraph/VirtualShadowRenderer.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Delete: `Engine/Runtime/Render/FrameUniformAllocator.h`
- Delete: `Engine/Runtime/Render/FrameUniformAllocator.cpp`
- Delete: `Engine/Runtime/Render/RenderFrameUniformCache.h`
- Delete: `Engine/Runtime/Render/RenderFrameUniformCache.cpp`
- Test: existing CTest targets only

- [x] **Step 1: Delete the three target fields and their APIs**

Remove from `FrameContext`:

```cpp
inFlightGpuFrameObjects_
uniformAllocator_
uniformCache_
RetainInFlightGpuFrameObject
UploadUniform
GetFrameUniform
GetViewUniform
GetObjectUniform
```

Remove their initialization, reset, shutdown, and forwarding code. Remove `<memory_resource>` and PMR includes no longer used by the file.

- [x] **Step 2: Delete obsolete source files and CMake entries**

Delete both old allocator/cache pairs and remove them from `CMake/Targets/Engine.cmake`. Point remaining `UniformBufferAllocation` includes to `RenderUniformBuffer.h`.

- [x] **Step 3: Run the implementation-symbol scan**

Run:

```text
rg -n "inFlightGpuFrameObjects_|FrameUniformAllocator|RenderFrameUniformCache|RetainInFlightGpuFrameObject|BaseRenderer::RetainInFlightGpuFrameObjects" Engine Editor Player Tests CMake
```

Expected: no output.

- [x] **Step 4: Build and commit the removal**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: build exits `0`.

Commit:

```text
git add -A CMake/Targets/Engine.cmake Engine/Runtime/Render Editor/RenderPass Engine/Render/PBR
git commit -m "render: remove frame context resource managers"
```

### Task 9: Format, run full regression verification, and document the result

**Files:**
- Modify if required by implementation decisions: `Docs/ArchitectureOverview.md`
- Modify: `Docs/Superpowers/Plans/2026-08-10-frame-context-resource-management.md` checkbox state
- Test: existing configure, build, CTest, D3D11/D3D12 Editor smoke paths

- [x] **Step 1: Format every changed C++ file**

Use the repository `.clang-format` on the changed `.h`, `.cpp`, and `.mm` files. Run `git diff --check` afterward.

Expected: formatter exits `0`; `git diff --check` produces no output.

- [x] **Step 2: Run static architecture checks**

Run:

```text
rg -n "inFlightGpuFrameObjects_|FrameUniformAllocator|RenderFrameUniformCache|RetainInFlightGpuFrameObject|BaseRenderer::RetainInFlightGpuFrameObjects" Engine Editor Player Tests CMake
rg -n "WaitForAllFrameContexts" Engine/Runtime/Render/RenderSystem.cpp
```

Expected: the first command has no output; the second lists only device/swapchain lifecycle synchronization.

- [x] **Step 3: Run the full Windows test build**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: configure/build exit `0`; CTest reports `100% tests passed`.

- [x] **Step 4: Run the Windows debug product build**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: `VEngine`, `VEnginePlayer`, and Windows Editor targets build successfully.

- [x] **Step 5: Run Editor rendering smoke checks with an explicit project**

Launch the built Editor with:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Check D3D12 and the repository's available D3D11 startup option. Confirm Scene View, Game View, mesh rendering, material constants, view movement, object transform updates, HDR tone mapping, VSM, SceneGrid, Gizmos, and shutdown complete without validation errors or lifetime assertions.

- [x] **Step 6: Update architecture documentation only where behavior changed**

If `Docs/ArchitectureOverview.md` still describes FrameContext retention or per-frame uniform allocation, replace that text with the finalized ownership model:

```text
Persistent RT owner / per-slot transient pool / fence retirement queue
```

Do not add a second design document.

- [x] **Step 7: Commit verification and documentation**

```text
git add Docs/ArchitectureOverview.md Docs/Superpowers/Plans/2026-08-10-frame-context-resource-management.md
git commit -m "docs: record frame resource ownership verification"
```

If `Docs/ArchitectureOverview.md` requires no edit and checkbox-only plan updates are not retained in the implementation branch, skip this commit and report the verified implementation commits instead.
