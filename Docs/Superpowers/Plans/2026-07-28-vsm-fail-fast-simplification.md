# VSM Fail-Fast Simplification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make VSM mandatory for every rendered view family and delete placeholder bindings, disabled-shadow branches, prepared-frame transactions, rollback, and legacy VSM scheduling.

**Architecture:** The renderer registers one complete VSM FrameGraph after all view depth producers. `VirtualShadowManager::AddToFrameGraph` has one successful path and all violated requirements call a shared non-returning error helper. Scene-cache mutations become immediate; statistics retain only the fence-delayed readback lifetime required by the GPU.

**Tech Stack:** C++20, CMake, VEngine FrameGraph/RHI, HLSL, D3D11, D3D12, CTest.

---

## Workspace Note

The workspace starts with interrupted, uncommitted pipeline-fallback work in:

- `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- `Tests/Render/RendererFrameGraphSmokeTests.cpp`

Those edits belong to this change. Convert or remove them with `apply_patch`; do not reset the worktree and do not disturb unrelated user changes.

All Windows commands run through:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat <command>'
```

## File Responsibility Map

- `VirtualShadowManager.*`: mandatory family preparation, scene-cache ownership, scheduling diagnostics, statistics-slot ownership.
- `VirtualShadowFrameGraph.*`: real scene-resource import and complete VSM graph registration.
- `VirtualShadowRenderer.*`: validation and binding of real sampling snapshots only.
- `VirtualShadowSceneCache.*`: scene-shared real atlas, page table, physical metadata, and statistics resources.
- `VirtualShadowViewCache.*` / `VirtualShadowTypes.h`: active-only packet and GPU constant layouts.
- `VirtualShadow*Passes.*`: focused graph registration and non-returning recording helpers.
- `RenderSystem.cpp` / `RenderFramePipelineData.h`: frame submission and statistics-fence notification without VSM transactions.
- `BasicMesh.hlsl`: unconditional VSM sampling using valid bindings.
- `Tests/Unit/VirtualShadowTests.cpp`: active-only CPU contracts.
- `Tests/Render/RendererFrameGraphSmokeTests.cpp`: production family topology and final sampling dependencies.
- `Tests/Render/VirtualShadowFrameGraphSmokeTests.cpp`: real D3D11/D3D12 VSM graph recording.

## Task 1: Remove Placeholder And Disabled Sampling State

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
- Modify: `Assets/Builtin/Shaders/BasicMesh.hlsl`
- Test: `Tests/Unit/VirtualShadowTests.cpp`

- [ ] **Step 1: Replace disabled/placeholder unit expectations with mandatory-binding expectations**

Delete tests for `PublishPlaceholderBindings`, `DisableResultWithPlaceholder`, disabled snapshots, mixed atlas selection, and placeholder binding. Add an active-only contract:

```cpp
bool TestMandatoryVirtualShadowSampling()
{
    TestTexture atlas(4096, 4096);
    TestSampler sampler;
    TestBuffer pageTable(ve::VirtualShadowLogicalPageBufferSize + 256);

    ve::VirtualShadowViewResult result;
    result.receiverDepthReady = true;
    result.packet.valid = true;
    result.packet.viewID = 19;
    result.packet.atlasExtent = 4096;
    result.packet.screenWidth = 1280;
    result.packet.screenHeight = 720;
    result.packet.clipmaps =
        ve::BuildVirtualShadowClipmaps(ve::Matrix44::Identity(), ve::Vector3::UnitZ(), 200.0f);
    result.atlas = &atlas;
    result.comparisonSampler = &sampler;
    result.pageTable = &pageTable;
    result.pageTableOffset = 256;
    result.pageTableSize = ve::VirtualShadowLogicalPageBufferSize;

    const ve::VirtualShadowSamplingSnapshot snapshot = ve::BuildVirtualShadowSamplingSnapshot(result);
    return Expect(snapshot.atlas == &atlas &&
                      snapshot.comparisonSampler == &sampler &&
                      snapshot.pageTable == &pageTable &&
                      snapshot.pageTableOffset == 256 &&
                      snapshot.pageTableSize == ve::VirtualShadowLogicalPageBufferSize,
                  "Mandatory VSM sampling should preserve the real scene bindings");
}
```

After implementation, the task's source scan must prove that neither `VirtualShadowGpuConstants` nor
`VirtualShadowFramePacket` contains an `enabled` member. The runtime test above supplies the RED condition because the
old builder rejects the default-disabled result instead of preserving its real bindings.

- [ ] **Step 2: Run the unit build and observe RED**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests'
```

Expected: compilation fails because the old `enabled` and placeholder members still exist and active-only APIs have not been simplified.

- [ ] **Step 3: Simplify the public sampling model**

Make the final result and snapshot active-only:

```cpp
struct VirtualShadowViewResult
{
    bool receiverDepthReady = false;
    VirtualShadowFramePacket packet;
    rhi::RhiTexture* atlas = nullptr;
    rhi::RhiSampler* comparisonSampler = nullptr;
    rhi::RhiBuffer* pageTable = nullptr;
    UInt64 pageTableOffset = 0;
    UInt64 pageTableSize = 0;
};

struct VirtualShadowSamplingSnapshot
{
    VirtualShadowGpuConstants constants;
    rhi::RhiTexture* atlas = nullptr;
    rhi::RhiSampler* comparisonSampler = nullptr;
    rhi::RhiBuffer* pageTable = nullptr;
    UInt64 pageTableOffset = 0;
    UInt64 pageTableSize = 0;
};

[[nodiscard]] VirtualShadowSamplingSnapshot BuildVirtualShadowSamplingSnapshot(const VirtualShadowViewResult& result) noexcept;
[[nodiscard]] rhi::RhiTexture* SelectVirtualShadowFamilyAtlas(std::span<const VirtualShadowSamplingSnapshot> snapshots) noexcept;
void BindVirtualShadowSampling(rhi::RhiCommandList& commandList,
                               const VirtualShadowSamplingSnapshot& snapshot,
                               const UniformBufferAllocation& constants);
```

`BuildVirtualShadowSamplingSnapshot`, family-atlas selection, and binding must validate every pointer, range, packet, clipmap, and extent through a common non-returning helper:

```cpp
[[noreturn]] void FailVirtualShadow(std::string_view message)
{
    VE_LOG_ERROR_CATEGORY("VirtualShadow", "{}", message);
    VE_ASSERT_ALWAYS_MESSAGE(false, message.data());
    std::terminate();
}
```

Do not return an empty snapshot and do not return `ErrorCode`.

- [ ] **Step 4: Remove physical placeholder resources**

Delete:

- manager `EnsurePlaceholderResources`, `PublishPlaceholderBindings`, `DisableResult`, and placeholder device/resource members;
- `ImportVirtualShadowPlaceholderFrameGraphResources`;
- scene-cache `placeholderAtlasTexture_` and its accessors;
- placeholder allocation from `VirtualShadowSceneCache::EnsureGpuResources`;
- mixed/disabled atlas normalization.

`VirtualShadowSceneCache::EnsureGpuResources` creates only the real atlas and buffers. Any failed creation calls the shared fatal helper.

- [ ] **Step 5: Remove VSM enabled flags from packets, constants, and shaders**

Delete `VirtualShadowFramePacket::enabled` and `VirtualShadowGpuConstants::enabled`. `VirtualShadowViewCache::PrepareFrame` must require `input.light.enabled`:

```cpp
if (!input.light.enabled)
{
    FailVirtualShadow("VSM requires an enabled shadow-casting directional light.");
}
```

Remove the `enabled` word from all embedded VSM HLSL constant definitions and their C++ upload builders.

In `Assets/Builtin/Shaders/BasicMesh.hlsl`, remove `virtualShadowEnabled` and only retain the per-object receive-shadows choice:

```hlsl
float ComputeVirtualShadowVisibility(float3 worldPosition, float3 worldNormal, uint objectReceivesShadows)
{
    if (objectReceivesShadows == 0u)
    {
        return 1.0f;
    }
    // Resolve and sample the mandatory VSM bindings.
}
```

The debug path must also sample the mandatory bindings without a disabled-shadow early return.

- [ ] **Step 6: Run active-only unit tests**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests'
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineVirtualShadowTests --output-on-failure'
```

Expected: build succeeds and the focused test passes.

- [ ] **Step 7: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.h Engine/Runtime/Render/VirtualShadow/VirtualShadowSceneCache.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.h Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp Assets/Builtin/Shaders/BasicMesh.hlsl Tests/Unit/VirtualShadowTests.cpp
git commit -m "render: require valid VSM sampling bindings"
```

## Task 2: Replace Prepared Transactions With Immediate Scene State

**Files:**

- Delete: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPreparedFrameScope.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipelineData.h`
- Modify: `Engine/Runtime/Render/FrameContext.h`
- Modify: `Engine/Runtime/Render/FrameContext.cpp`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Test: `Tests/Unit/VirtualShadowTests.cpp`
- Test: `Tests/Render/RendererFrameGraphSmokeTests.cpp`

- [ ] **Step 1: Write transaction-removal compile contracts**

Remove prepared-frame tests and the `VirtualShadowPreparedFrameScope` include. Add contracts around the final manager interface:

```cpp
static_assert(std::is_same_v<
              decltype(&ve::VirtualShadowManager::NotifyFrameSubmitted),
              void (ve::VirtualShadowManager::*)(ve::UInt64, ve::rhi::RhiFence&, ve::UInt64)>);
```

Update the default frame-data test so it no longer references `virtualShadowPreparedFrame`.

Update renderer topology expectations:

```cpp
const ve::VirtualShadowSchedulingDiagnostics diagnostics = manager.GetSchedulingDiagnostics(frameData.frameIndex);
passed &= Expect(diagnostics.graphRegistrarInvocationCount == 1 &&
                     diagnostics.sceneFramePreparationCount == 1,
                 "One family registrar should apply scene state immediately");
```

- [ ] **Step 2: Run focused builds and observe RED**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineRendererFrameGraphSmokeTests'
```

Expected: compilation fails on the removed ticket/scope API and missing `NotifyFrameSubmitted`.

- [ ] **Step 3: Delete the prepared-frame protocol**

Delete:

- `VirtualShadowPreparedFrameTicket`;
- all `SceneState` prepared/last-prepared members;
- `TryBeginFrame`, `BeginPreparedFrame`, `CommitPreparedFrame`, `AbortPreparedFrame`, and `RollbackFramePreparation`;
- transaction counters from `VirtualShadowSchedulingDiagnostics`;
- `VirtualShadowPreparedFrameScope.h`;
- `FrameRenderPipelineData::virtualShadowPreparedFrame`;
- manager `CommitPreparedFrame` and `AbortPreparedFrame`.

Keep reset generation simple and immediate:

```cpp
void ConsumeReset() noexcept
{
    consumedResetGeneration_ = resetGeneration_;
}
```

After family preparation has consumed the current reset request, call `ConsumeReset()` directly. There is no rollback.

- [ ] **Step 4: Simplify RenderSystem frame execution**

Remove the prepared ticket, rollback scope, commit helper, abort helper, and every failure cleanup branch. The frame flow becomes:

```cpp
[[noreturn]] void FailRenderSystem(std::string_view message)
{
    VE_LOG_ERROR("{}", message);
    VE_ASSERT_ALWAYS_MESSAGE(false, message.data());
    std::terminate();
}

void RequireSuccess(ErrorCode result, std::string_view message)
{
    if (result != ErrorCode::None)
    {
        FailRenderSystem(message);
    }
}

FrameRenderPipelineData frameData = {};
RequireSuccess(PrepareMainSwapchainFrame(impl, frameData), "RenderSystem failed to prepare the frame.");
framePipeline->RenderFrame(frameData);
RequireSuccess(SubmitMainSwapchainFrame(impl, frameData, framePipeline), "RenderSystem failed to submit the frame.");

const UInt32 frameSlotIndex = static_cast<UInt32>(frameData.frameIndex % RenderFrameContextCount);
impl.submittedFrameIndices[frameSlotIndex] = frameData.frameIndex;
frameData.virtualShadowManager->NotifyFrameSubmitted(
    frameData.frameIndex, frameContext.GetCompletionFence(), frameContext.GetSubmittedFenceValue());

RequireSuccess(PresentMainSwapchainFrame(impl), "RenderSystem failed to present the frame.");
```

Expose the exact stored value without duplicating fence ownership:

```cpp
[[nodiscard]] UInt64 FrameContext::GetSubmittedFenceValue() const noexcept
{
    return submittedFenceValue_;
}
```

- [ ] **Step 5: Run focused tests**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineRendererFrameGraphSmokeTests'
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat ctest --preset windows-msvc-tests -R "VEngine(VirtualShadowTests|RendererFrameGraphSmokeTests)" --output-on-failure'
```

Expected: both tests pass and `rg "PreparedFrame|virtualShadowPreparedFrame|RollbackFramePreparation" Engine Tests` finds no VSM lifecycle symbols.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp Engine/Runtime/Render/RenderFramePipelineData.h Engine/Runtime/Render/FrameContext.h Engine/Runtime/Render/FrameContext.cpp Engine/Runtime/Render/RenderSystem.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowPreparedFrameScope.h CMake/Targets/Engine.cmake Tests/Unit/VirtualShadowTests.cpp Tests/Render/RendererFrameGraphSmokeTests.cpp
git commit -m "render: remove VSM prepared transactions"
```

## Task 3: Make Statistics A One-Way Fence Lifecycle

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Test: `Tests/Unit/VirtualShadowTests.cpp`
- Test: `Tests/Render/VirtualShadowFrameGraphSmokeTests.cpp` after Task 5 rename, or current `VirtualShadowGpuPipelineSmokeTests.cpp` before the rename
- Test: `Tests/Unit/RenderPerformanceStatisticsTests.cpp`

- [ ] **Step 1: Replace statistics transaction tests**

Delete tests for begin/commit/abort/unavailable fallback. Add slot-reuse and fence-delay expectations:

```cpp
// Add this method to the existing recording fence used by the test.
void Complete(ve::UInt64 value) noexcept
{
    completedValue_ = value;
}

manager.PrepareFrameStatistics(frameIndex, sceneIdentity);
manager.NotifyFrameSubmitted(frameIndex, fence, submittedFence);

passed &= Expect(!manager.ConsumeCompletedFrameStatistics(device, frameIndex).has_value(),
                 "Statistics must remain unreadable before the submitted fence completes");
fence.Complete(submittedFence);
passed &= Expect(manager.ConsumeCompletedFrameStatistics(device, frameIndex).has_value(),
                 "Statistics should become readable after the submitted fence completes");
```

The mock device/fence values must make the two states deterministic.

- [ ] **Step 2: Run focused tests and observe RED**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineRenderPerformanceStatisticsTests'
```

Expected: compilation fails because the old statistics transaction API is still present and the one-way submission API is incomplete.

- [ ] **Step 3: Implement the readback slot state**

Use one state per readback slot:

```cpp
struct StatisticsReadbackSlot
{
    std::unique_ptr<rhi::RhiBuffer> buffer;
    UInt64 sourceFrameIndex = 0;
    UInt64 sceneIdentity = 0;
    rhi::RhiFence* completionFence = nullptr;
    UInt64 submittedFence = 0;
    bool copyRecorded = false;
};
```

`PrepareFrameStatistics`:

- selects `frameIndex % slotCount`;
- asserts `copyRecorded == false` or the previous submitted fence is complete;
- stores frame and scene identity;
- sets `copyRecorded = true`;
- never publishes an unavailable fallback.

`NotifyFrameSubmitted(frameIndex, completionFence, fenceValue)`:

- locates the slot;
- asserts frame identity and `copyRecorded`;
- stores the stable `FrameContext` fence pointer and non-zero fence value.

`ConsumeCompletedFrameStatistics`:

- returns no value while `completionFence->GetCompletedValue() < submittedFence`;
- maps and publishes the completed GPU statistics after the fence;
- clears `copyRecorded`, `completionFence`, and `submittedFence`;
- has no abort/reset retry branch.

- [ ] **Step 4: Run statistics and renderer tests**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineRenderPerformanceStatisticsTests VEngineRendererFrameGraphSmokeTests'
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat ctest --preset windows-msvc-tests -R "VEngine(VirtualShadow|RenderPerformanceStatistics|RendererFrameGraph)" --output-on-failure'
```

Expected: focused tests pass and `rg "BeginFrameStatistics|CommitFrameStatistics|AbortFrameStatistics" Engine Tests` has no matches.

- [ ] **Step 5: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp Engine/Runtime/Render/RenderSystem.cpp Tests/Unit/VirtualShadowTests.cpp Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp Tests/Unit/RenderPerformanceStatisticsTests.cpp
git commit -m "render: simplify VSM statistics readback lifecycle"
```

## Task 4: Enforce One Mandatory Manager Graph Path

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/MobileRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- Test: `Tests/Render/RendererFrameGraphSmokeTests.cpp`

- [ ] **Step 1: Replace disabled/Metal tests with a complete mandatory-family test**

Delete all-disabled, mixed-family, pipeline-fallback, and Metal-placeholder success cases.

The production two-view test must assert:

```cpp
passed &= Expect(diagnostics.graphRegistrarInvocationCount == 1 &&
                     diagnostics.legacyPreRenderInvocationCount == 0 &&
                     diagnostics.sceneStateAcquisitionCount == 1 &&
                     diagnostics.sceneFramePreparationCount == 1,
                 "One mandatory VSM registrar should own the family");

passed &= Expect(CountPass("DepthPrePass") == 2 &&
                     CountPass("VirtualShadow.ClearScene") == 1 &&
                     CountPass("VirtualShadow.FinalizeScene") == 1 &&
                     CountPass("VirtualShadow.StatisticsReadback") == 1,
                 "Two views should produce one complete family VSM graph");
```

Assert Opaque and Transparent read the exact final atlas version and the exact final page-table version for their view.

- [ ] **Step 2: Run the renderer smoke build and observe RED**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests'
```

Expected: compilation or assertions fail because manager setup still contains disabled/placeholder branches.

- [ ] **Step 3: Convert pipeline preflight to a non-returning requirement**

Replace the interrupted fallback draft:

```cpp
[[nodiscard]] bool PreflightVirtualShadowFrameGraphPipelines(...);
```

with:

```cpp
void RequireVirtualShadowFrameGraphPipelines(const FrameRenderPipelineData& frameData);
```

Implementation:

```cpp
void RequireVirtualShadowFrameGraphPipelines(const FrameRenderPipelineData& frameData)
{
    if (!virtual_shadow_detail::ValidateVirtualShadowRequestPipelines(frameData) ||
        !virtual_shadow_detail::ValidateVirtualShadowResidencyPipelines(frameData) ||
        !virtual_shadow_detail::ValidateVirtualShadowPagePipelines(frameData))
    {
        FailVirtualShadow("VSM failed to create the mandatory shader pipelines.");
    }
}
```

The manager calls this before mutating scene cache state. There is no `false` return and no continue-without-shadows behavior.

- [ ] **Step 4: Remove every optional branch from `AddToFrameGraph`**

Add two focused helpers in `VirtualShadowManager.cpp`:

```cpp
void RequireVirtualShadow(bool condition, std::string_view message)
{
    if (!condition)
    {
        FailVirtualShadow(message);
    }
}

[[nodiscard]] std::shared_ptr<RTLight> GetRequiredShadowDirectionalLight(const RendererData& rendererData)
{
    std::shared_ptr<RTLight> selected = FindPrimaryDirectionalLight(rendererData.scene);
    RequireVirtualShadow(selected != nullptr, "VSM requires a directional light.");
    RequireVirtualShadow(selected->CastShadows(), "VSM requires directional-light shadows to be enabled.");
    return selected;
}
```

Use the existing scene light iteration to implement `FindPrimaryDirectionalLight`; if the exact RTLight accessors differ,
use their existing names without adding a second light-selection policy.

At entry, require:

```cpp
RequireVirtualShadow(frameData.device != nullptr, "VSM requires an RHI device.");
RequireVirtualShadow(frameData.shaderManager != nullptr, "VSM requires a shader manager.");
RequireVirtualShadow(frameData.device->GetBackend() != rhi::RhiBackend::Metal, "Metal VSM is not implemented.");
RequireVirtualShadow(rendererData.scene != nullptr, "VSM requires a render scene.");
RequireVirtualShadow(graphData.views.size() == rendererData.views.size(), "VSM renderer/graph view counts differ.");
RequireVirtualShadow(!rendererData.views.empty(), "VSM requires at least one renderer view.");
const std::shared_ptr<RTLight> light = GetRequiredShadowDirectionalLight(rendererData);
```

For every view require camera, stable ID, receiver-depth handle with a produced version, valid packet, distinct page-table slice, and real snapshot.

Delete:

- backend disabled return;
- no-light return;
- light-disabled return;
- candidate skipping;
- placeholder imports;
- disabled snapshots;
- resource-creation recovery;
- exception rollback.

The only exit from `AddToFrameGraph` is successful complete registration.

- [ ] **Step 5: Make scene passes require graph handles**

Opaque and Transparent setup unconditionally declare:

```cpp
passData.virtualShadowAtlas = builder.Read(graphData.virtualShadowAtlas);
passData.virtualShadowPageTable = builder.Read(viewGraphData.virtualShadowPageTable);
```

Invalid handles, mismatched physical resources, uniform upload failure, or binding failure call the non-returning helper. Do not retain conditional reads or `ErrorCode` propagation.

- [ ] **Step 6: Run production topology tests**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests VEngineVirtualShadowTests'
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat ctest --preset windows-msvc-tests -R "VEngine(RendererFrameGraph|VirtualShadowTests)" --output-on-failure'
```

Expected: both pass. Source scans must show no `VirtualShadowPlaceholder`, `DisableResult`, `NormalizeDisabledVirtualShadowFamilyAtlases`, or `PreflightVirtualShadowFrameGraphPipelines`.

- [ ] **Step 7: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.h Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp Engine/Runtime/Render/Renderer/MobileRenderer.cpp Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp Tests/Render/RendererFrameGraphSmokeTests.cpp
git commit -m "render: make family VSM mandatory"
```

## Task 5: Delete Legacy Scheduling And Finish Void Error Boundaries

**Files:**

- Delete: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.h`
- Delete: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
- Rename: `Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp` to `Tests/Render/VirtualShadowFrameGraphSmokeTests.cpp`
- Rename: `CMake/Targets/Tests/VirtualShadowGpuPipelineSmokeTests.cmake` to `CMake/Targets/Tests/VirtualShadowFrameGraphSmokeTests.cmake`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `CMake/Targets/Tests.cmake`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.h`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.cpp`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Test: `Tests/Render/VirtualShadowFrameGraphSmokeTests.cpp`
- Test: `Tests/Render/RendererFrameGraphSmokeTests.cpp`

- [ ] **Step 1: Rename the smoke test and remove legacy API assertions**

The renamed smoke test directly registers:

```cpp
manager.AddToFrameGraph(frameData, frameGraph, rendererData, graphData);
```

Delete all calls to `PreRenderShadowStep`, `ClearSceneFrame`, `PrepareViewRequests`, `ResolveViewHits`, `AllocateViewPages`, `RenderViewPages`, `FinalizeSceneFrame`, and `CopySceneStatistics`.

Retain real D3D11/D3D12 execution and add a real indexed caster to every active view:

```cpp
ve::RTMeshResourceDesc meshDesc;
meshDesc.name = "VirtualShadowSmokeTriangle";
meshDesc.vertices = {
    {{-1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
    {{1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}},
    {{0.0F, 0.0F, 1.0F}, {0.0F, 1.0F, 0.0F}},
};
meshDesc.indices = {0, 1, 2};

auto mesh = std::make_shared<ve::RTMeshResource>(meshDesc);
mesh->InitRenderResource(device, meshDesc);

ve::RTRenderItemInitParam itemDesc;
itemDesc.meshResource = mesh;
itemDesc.renderItemID = 1;
itemDesc.castShadows = true;
rendererData.opaqueItems.push_back(std::make_shared<ve::RTRenderItem>(std::move(itemDesc)));
```

The family-level opaque list is consumed by each view's caster pass. Assert at least one indexed caster draw is recorded per view.

- [ ] **Step 2: Run the renamed test build and observe RED**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowFrameGraphSmokeTests'
```

Expected: target/source references still point at the old GPU-pipeline test and legacy implementation.

- [ ] **Step 3: Delete legacy production code**

Delete `VirtualShadowGpuPipeline.*`, manager `PreRenderShadowStep`, legacy diagnostics counters, compatibility recording adapters, and CMake references.

Keep only focused helpers required by FrameGraph execute callbacks. Move any still-shared context/constants builder out of the deleted legacy file into the appropriate request/residency/page-pass source.

- [ ] **Step 4: Make renderer flow functions void**

Final interfaces:

```cpp
class FrameRenderPipeline
{
public:
    virtual void RenderFrame(const FrameRenderPipelineData& frameData) = 0;
};

class BaseRenderer
{
public:
    void RenderScene();
};
```

Editor/Player pipelines and `RenderSystem::RenderMainSwapchainFrame` call these functions without checking `ErrorCode`. FrameGraph compile/execute, command-list begin/end, submission, and present failures use `RequireSuccess`:

```cpp
void RequireSuccess(ErrorCode result, std::string_view message)
{
    if (result != ErrorCode::None)
    {
        VE_LOG_ERROR("{}", message);
        VE_ASSERT_ALWAYS_MESSAGE(false, message.data());
        std::terminate();
    }
}
```

RHI APIs may retain their native `bool`/`ErrorCode`; render orchestration does not propagate them.

- [ ] **Step 5: Run focused and full Windows tests**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests VEngineVirtualShadowTests VEngineVirtualShadowFrameGraphSmokeTests'
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat ctest --preset windows-msvc-tests -R "VEngine(RendererFrameGraph|VirtualShadow)" --output-on-failure'
```

Expected: all focused tests pass and:

```text
rg "VirtualShadowGpuPipeline|PreRenderShadowStep|VirtualShadowPreparedFrame|placeholderAtlas|placeholderPageTable" Engine Tests CMake
```

returns no VSM implementation matches.

- [ ] **Step 6: Commit**

```text
git add -A Engine/Runtime/Render/VirtualShadow Engine/Runtime/Render/RenderFramePipeline.h Engine/Runtime/Render/RenderFramePipeline.cpp Engine/Runtime/Render/Renderer/BaseRenderer.h Engine/Runtime/Render/Renderer/BaseRenderer.cpp Engine/Runtime/Render/RenderSystem.cpp Tests/Render CMake/Targets
git commit -m "render: remove legacy and recoverable VSM paths"
```

## Task 6: Update Canonical Documentation And Run Final Verification

**Files:**

- Modify: `Docs/ArchitectureOverview.md`
- Modify: `Docs/DevelopmentPlan.md`
- Modify: `Docs/RenderSystemDesign.md`
- Modify: `Docs/Superpowers/Specs/2026-07-28-view-family-frame-graph-renderer-design.md`
- Test: full Windows build and CTest suite

- [ ] **Step 1: Update canonical documentation**

Document:

- renderer owns one family FrameGraph;
- VSM is mandatory on every renderer path;
- Metal currently terminates because VSM is unimplemented;
- no direction light or disabled shadows are fatal;
- scene cache mutates immediately with no prepared transaction;
- statistics use one-way fence-delayed readback;
- all VSM failures log and terminate;
- no placeholder, disabled, rollback, retry, or legacy pre-render path exists.

Replace the old `Docs/DevelopmentPlan.md` failure policy:

```text
GPU-driven VSM is mandatory. D3D11 and D3D12 execute the complete FrameGraph path.
Metal, missing lights, disabled shadows, and VSM resource or pipeline failures are unrecoverable
rendering errors that log context and terminate the process.
```

- [ ] **Step 2: Scan for removed concepts**

Run:

```text
rg -n "VirtualShadowPreparedFrame|PreRenderShadowStep|VirtualShadowGpuPipeline|placeholderAtlas|placeholderPageTable|VirtualShadowPlaceholder|DisableResultWithPlaceholder|NormalizeDisabledVirtualShadowFamilyAtlases|virtualShadowEnabled" Engine Tests CMake Assets/Builtin/Shaders
```

Expected: no matches.

`placeholder` may remain in unrelated editor/footer documentation or generic code; only VSM matches are prohibited.

- [ ] **Step 3: Configure and build all Windows targets**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --preset windows-msvc-tests'
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests'
```

Expected: successful build.

- [ ] **Step 4: Run the complete CTest suite**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure'
```

Expected: all tests pass.

- [ ] **Step 5: Build Editor and Player**

Run:

```text
cmd /d /c 'set "PATH=" & CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor VEngineWinPlayer'
```

Expected: both applications build.

- [ ] **Step 6: Run deterministic Editor startup smoke**

Launch:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Expected: on D3D11/D3D12 the project reaches the first rendered frame with the complete VSM graph. Do not use project-selection UI automation.

- [ ] **Step 7: Run final diff checks**

Run:

```text
git diff --check
git status --short
```

Expected: no whitespace errors; only intended documentation changes remain before the final commit.

- [ ] **Step 8: Commit documentation**

```text
git add Docs/ArchitectureOverview.md Docs/DevelopmentPlan.md Docs/RenderSystemDesign.md Docs/Superpowers/Specs/2026-07-28-view-family-frame-graph-renderer-design.md
git commit -m "docs: describe mandatory fail-fast VSM"
```

- [ ] **Step 9: Final review**

Use the requesting-code-review workflow on the complete implementation range. Resolve every Critical or Important finding, rerun the affected focused tests, then rerun the full CTest suite.
