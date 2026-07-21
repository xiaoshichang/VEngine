# Virtual Shadow Maps Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a first-stage, per-view Virtual Shadow Maps implementation for one directional light, with CPU-managed physical pages and matching behavior on D3D11, D3D12, and Metal.

**Architecture:** Each Scene, Game, or Player view owns a persistent `RenderViewState` whose render-thread proxy owns one fixed physical depth atlas, CPU page cache, resident-page table, invalidation history, and statistics. Every frame still creates a short-lived renderer and FrameGraph; it imports the view-owned atlas, renders only dirty pages, then samples the resident-page table from the normal forward passes. The RHI gains only the bounded prerequisites needed by this feature: sampled depth, depth-only passes, vertex-only pipelines, and an explicit small resource layout.

**Tech Stack:** C++20, CMake, VEngine FrameGraph and common RHI, HLSL/FXC/DXC/Slang, D3D11, D3D12, Metal, MSVC/CTest through `CMake/Scripts/WithMsvc.bat`.

---

## File map

New math and camera files:

- `Engine/Runtime/Math/Bounds.h`: finite AABB construction, transformation, and intersection.
- `Engine/Runtime/Math/Plane.h`: normalized plane and signed-distance operations.
- `Engine/Runtime/Math/Frustum.h/.cpp`: D3D zero-to-one frustum extraction and AABB classification.
- `Engine/Runtime/Render/RenderCameraMath.h/.cpp`: shared perspective, orthographic, rigid-view, and view-projection construction.

New view-state and virtual-shadow files:

- `Engine/Runtime/Render/RenderViewState.h/.cpp`: Scene Thread handle and Render Thread persistent view state.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`: configuration, page keys, requests, GPU entries, and statistics.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.h/.cpp`: 2048-entry bounded open-addressed resident table.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h/.cpp`: free list, pinning, dirty state, and LRU eviction.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h/.cpp`: stable light basis, level origins, depth epoch, and projections.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.h/.cpp`: receiver culling, page request generation, and priority.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h/.cpp`: per-view old/new caster-bounds invalidation.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h/.cpp`: persistent atlas, sampler, CPU cache orchestration, caster lists, GPU upload packet, and diagnostics.
- `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.h/.cpp`: page-local depth clear and caster draws.

New shader assets:

- `Assets/Builtin/Shaders/VirtualShadowClear.hlsl`: fullscreen-triangle vertex shader used to clear one physical page.
- `Assets/Builtin/Shaders/VirtualShadowClear.veshader` and `.meta`: runtime shader asset metadata.

New tests:

- `Tests/Unit/VirtualShadowTests.cpp`: math, keys, hash table, LRU, request generation, invalidation, and view isolation.
- `CMake/Targets/Tests/VirtualShadowTests.cmake`: CTest target registration.
- `Tests/Smoke/RhiVirtualShadowSmokeTests.cpp`: hidden-window D3D11/D3D12 command-recording smoke test.
- `CMake/Targets/Tests/RhiVirtualShadowSmokeTests.cmake`: smoke target registration.

Existing integration points:

- Common RHI: `Engine/RHI/Common/RhiTypes.h`, `RhiDevice.h`, `RhiTypes.cpp`.
- Backends: `Engine/RHI/D3D11/D3D11Rhi.cpp`, `Engine/RHI/D3D12/D3D12Rhi.cpp`, `Engine/RHI/Metal/MetalRhi.mm`.
- FrameGraph: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h/.cpp`, `FrameGraphBuilder.h/.cpp`.
- Scene proxies: `Engine/Runtime/Render/RenderScene.h/.cpp`, `Engine/Runtime/Scene/MeshRenderComponent.h/.cpp`, `LightComponent.h/.cpp`.
- Renderer: `Engine/Runtime/Render/Renderer/BaseRenderer.h/.cpp`, `StandaloneRenderer.h/.cpp`, `MobileRenderer.h/.cpp`, `RenderPass.h`, `OpaqueSceneRenderPass.h/.cpp`, `TransparentSceneRenderPass.h/.cpp`.
- View ownership: `Engine/Runtime/Render/RenderFramePipeline.h/.cpp`, `Editor/Core/Editor.h/.cpp`, `Editor/Panels/SceneViewPanel/SceneViewPanel.h/.cpp`, `Editor/Panels/GameViewPanel/GameViewPanel.h/.cpp`, `Engine/Runtime/Scene/SceneSystem.cpp`.
- Shader integration: `Assets/Builtin/Shaders/BasicMesh.hlsl`, `BasicMesh.veshader`, `Engine/Runtime/Render/RenderShaderIDs.h`, `ShaderManager.h/.cpp`.
- Build and docs: `CMake/Targets/Engine.cmake`, `CMake/Targets/Tests.cmake`, `Docs/RenderSystemDesign.md`, `Docs/DevelopmentPlan.md`.

## Task 1: Establish a clean baseline

**Files:**

- Inspect: `Docs/Superpowers/Specs/2026-07-21-virtual-shadow-maps-design.md`
- Inspect: `Docs/RenderSystemDesign.md`
- Inspect: `CMakePresets.json`

- [ ] **Step 1: Configure the Windows test preset**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-tests
```

Expected: configure succeeds and generates `Build/windows-msvc-tests`.

- [ ] **Step 2: Build the unmodified baseline**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
```

Expected: all existing targets build successfully.

- [ ] **Step 3: Run the unmodified test suite**

Run:

```text
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: every registered test passes. Record any pre-existing failure before changing code.

## Task 2: Add bounds, planes, frusta, and shared camera projection math

**Files:**

- Create: `Engine/Runtime/Math/Bounds.h`
- Create: `Engine/Runtime/Math/Plane.h`
- Create: `Engine/Runtime/Math/Frustum.h`
- Create: `Engine/Runtime/Math/Frustum.cpp`
- Create: `Engine/Runtime/Render/RenderCameraMath.h`
- Create: `Engine/Runtime/Render/RenderCameraMath.cpp`
- Modify: `Engine/Runtime/Render/RenderFrameUniformCache.cpp`
- Modify: `Tests/Unit/MathTests.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Write failing bounds and frustum tests**

Add these cases to `Tests/Unit/MathTests.cpp` and call them from `main()`:

```cpp
bool TestBoundsTransform()
{
    const ve::Aabb local = ve::Aabb::FromCenterExtents(ve::Vector3::Zero(), ve::Vector3(1.0f, 2.0f, 3.0f));
    const ve::Aabb world = local.Transformed(ve::Matrix44::Translation(ve::Vector3(5.0f, 0.0f, -2.0f)) *
                                                ve::Matrix44::RotationY(ve::Math::HalfPi));
    return Expect(world.GetCenter().IsNearlyEqual(ve::Vector3(5.0f, 0.0f, -2.0f)), "AABB center should transform") &&
           Expect(world.GetExtents().IsNearlyEqual(ve::Vector3(3.0f, 2.0f, 1.0f)), "AABB extents should include rotation");
}

bool TestPerspectiveAndOrthographicFrusta()
{
    const ve::Matrix44 perspective = ve::BuildPerspectiveProjection(ve::ToRadians(60.0f), 16.0f / 9.0f, 0.1f, 100.0f);
    const ve::Matrix44 orthographic = ve::BuildOrthographicProjection(10.0f, 1.0f, 0.1f, 100.0f);
    const ve::Frustum perspectiveFrustum = ve::Frustum::FromViewProjection(perspective);
    const ve::Frustum orthographicFrustum = ve::Frustum::FromViewProjection(orthographic);
    const ve::Aabb inside = ve::Aabb::FromCenterExtents(ve::Vector3(0.0f, 0.0f, 5.0f), ve::Vector3::One());
    const ve::Aabb outside = ve::Aabb::FromCenterExtents(ve::Vector3(1000.0f, 0.0f, 5.0f), ve::Vector3::One());
    return Expect(perspectiveFrustum.Intersects(inside), "Perspective frustum should contain the near object") &&
           Expect(!perspectiveFrustum.Intersects(outside), "Perspective frustum should reject the far-X object") &&
           Expect(orthographicFrustum.Intersects(inside), "Orthographic frustum should contain the near object") &&
           Expect(!orthographicFrustum.Intersects(outside), "Orthographic frustum should reject the far-X object");
}
```

- [ ] **Step 2: Run the math test and verify failure**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineMathTests
```

Expected: compilation fails because `Aabb`, `Frustum`, and projection helpers do not exist.

- [ ] **Step 3: Define the public math contracts**

Use these exact interfaces:

```cpp
class Aabb
{
public:
    Aabb() noexcept = default;
    Aabb(Vector3 minimum, Vector3 maximum) noexcept;
    [[nodiscard]] static Aabb FromCenterExtents(const Vector3& center, const Vector3& extents) noexcept;
    [[nodiscard]] bool IsFiniteAndValid() const noexcept;
    [[nodiscard]] const Vector3& GetMinimum() const noexcept;
    [[nodiscard]] const Vector3& GetMaximum() const noexcept;
    [[nodiscard]] Vector3 GetCenter() const noexcept;
    [[nodiscard]] Vector3 GetExtents() const noexcept;
    [[nodiscard]] Aabb Transformed(const Matrix44& matrix) const noexcept;
    [[nodiscard]] bool Intersects(const Aabb& other) const noexcept;
private:
    Vector3 minimum_ = Vector3::Zero();
    Vector3 maximum_ = Vector3::Zero();
};

class Plane
{
public:
    Plane() noexcept = default;
    Plane(Vector3 normal, Float32 distance) noexcept;
    [[nodiscard]] Plane Normalized() const noexcept;
    [[nodiscard]] Float32 SignedDistance(const Vector3& point) const noexcept;
    [[nodiscard]] const Vector3& GetNormal() const noexcept;
private:
    Vector3 normal_ = Vector3::UnitZ();
    Float32 distance_ = 0.0f;
};

class Frustum
{
public:
    [[nodiscard]] static Frustum FromViewProjection(const Matrix44& matrix) noexcept;
    [[nodiscard]] bool Intersects(const Aabb& bounds) const noexcept;
private:
    std::array<Plane, 6> planes_;
};
```

- [ ] **Step 4: Implement D3D zero-to-one frustum extraction**

Extract planes from matrix rows using these equations, normalize every plane, and reject an AABB only when its positive vertex is outside one plane:

```cpp
planes_[0] = MakePlane(matrix, 3, +1.0f, 0); // left:   row3 + row0
planes_[1] = MakePlane(matrix, 3, -1.0f, 0); // right:  row3 - row0
planes_[2] = MakePlane(matrix, 3, +1.0f, 1); // bottom: row3 + row1
planes_[3] = MakePlane(matrix, 3, -1.0f, 1); // top:    row3 - row1
planes_[4] = Plane(Vector3(matrix.Get(2, 0), matrix.Get(2, 1), matrix.Get(2, 2)), matrix.Get(2, 3)).Normalized();
planes_[5] = MakePlane(matrix, 3, -1.0f, 2); // far:    row3 - row2
```

Implement `Aabb::Transformed` by transforming all eight corners and taking component-wise minima and maxima; do not transform extents with an inverse or assume uniform scale.

- [ ] **Step 5: Extract projection construction from the uniform cache**

Expose:

```cpp
[[nodiscard]] Matrix44 BuildPerspectiveProjection(Float32 verticalFieldOfViewRadians,
                                                  Float32 aspectRatio,
                                                  Float32 nearClip,
                                                  Float32 farClip) noexcept;
[[nodiscard]] Matrix44 BuildOrthographicProjection(Float32 height, Float32 aspectRatio, Float32 nearClip, Float32 farClip) noexcept;
[[nodiscard]] Matrix44 BuildRigidView(const Matrix44& localToWorld) noexcept;
[[nodiscard]] Matrix44 BuildCameraViewProjection(const RTCamera& camera, rhi::RhiExtent2D targetExtent) noexcept;
```

Move the current formulas from `RenderFrameUniformCache.cpp` without changing matrix handedness or the zero-to-one depth convention. Replace its private `BuildViewProjection` call with `BuildCameraViewProjection`.

- [ ] **Step 6: Register, build, and run math tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineMathTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-tests -C Debug -R VEngineMathTests --output-on-failure
```

Expected: build succeeds and `VEngineMathTests` passes.

- [ ] **Step 7: Commit the math foundation**

```text
git add Engine/Runtime/Math Engine/Runtime/Render/RenderCameraMath.h Engine/Runtime/Render/RenderCameraMath.cpp Engine/Runtime/Render/RenderFrameUniformCache.cpp Tests/Unit/MathTests.cpp CMake/Targets/Engine.cmake
git commit -m "math: add bounds and frustum primitives"
```

## Task 3: Add the bounded RHI resource-layout and depth-only contracts

**Files:**

- Modify: `Engine/RHI/Common/RhiTypes.h`
- Modify: `Engine/RHI/Common/RhiTypes.cpp`
- Modify: `Engine/RHI/Common/RhiDevice.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.cpp`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h`

- [ ] **Step 1: Define an explicit small pipeline resource layout**

Add these descriptors to `RhiTypes.h`:

```cpp
enum class RhiPipelineResourceKind
{
    UniformBuffer,
    SampledTexture,
    Sampler,
};

struct RhiPipelineResourceBindingDesc
{
    RhiPipelineResourceKind kind = RhiPipelineResourceKind::UniformBuffer;
    RhiShaderStage stage = RhiShaderStage::Vertex;
    uint32_t slot = 0;
};

struct RhiPipelineResourceLayoutDesc
{
    const RhiPipelineResourceBindingDesc* bindings = nullptr;
    uint32_t bindingCount = 0;
};
```

Add `resourceLayout` and an explicit `colorAttachmentCount` to `RhiGraphicsPipelineDesc`:

```cpp
RhiPipelineResourceLayoutDesc resourceLayout = {};
uint32_t colorAttachmentCount = 1;
RhiFormat colorFormat = RhiFormat::Bgra8Unorm;
```

The first implementation accepts only zero or one color attachment and rejects duplicate `(kind, stage, slot)` entries.

- [ ] **Step 2: Distinguish swapchain color from no color attachment**

Change `RhiRenderPassBeginInfo` to carry:

```cpp
RhiRenderPassColorAttachmentInfo colorAttachment = {};
RhiRenderPassDepthAttachmentInfo depthAttachment = {};
bool hasColorAttachment = true;
bool colorAttachmentIsSwapchain = true;
bool hasDepthAttachment = false;
```

A null color texture is legal only when both `hasColorAttachment` and `colorAttachmentIsSwapchain` are true. A VSM pass sets `hasColorAttachment = false` and `colorAttachmentIsSwapchain = false`.

- [ ] **Step 3: Make FrameGraph accept depth-only raster passes**

Update validation to enforce this exact rule:

```cpp
if (!pass.colorAttachment.has_value() && !pass.depthAttachment.has_value())
{
    return Error(ErrorCode::InvalidArgument, "Frame graph raster pass '" + pass.name + "' requires at least one attachment.");
}
```

When color is absent, derive render area, viewport, and scissor from the depth descriptor. Set `RenderPassExecutionInfo::colorFormat` to `RhiFormat::Unknown`, `colorAttachmentCount` to zero, and `depthEnabled` to true. Guard color store-action inference and color attachment resolution with `has_value()`.

- [ ] **Step 4: Preserve existing swapchain semantics explicitly**

In `FrameGraph::Impl::BuildRenderPassBeginInfo`, set:

```cpp
beginInfo.hasColorAttachment = pass.colorAttachment.has_value();
if (pass.colorAttachment.has_value())
{
    const ResolvedFrameGraphTexture resolved = ResolveTexture(pass.colorAttachment->handle);
    beginInfo.colorAttachment.texture = resolved.texture;
    beginInfo.colorAttachmentIsSwapchain = resolved.isSwapchain;
}
else
{
    beginInfo.colorAttachmentIsSwapchain = false;
}
```

- [ ] **Step 5: Build the common contract**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngine
```

Expected: backend compilation fails only where the new explicit fields and optional fragment shader must be handled; common and FrameGraph code compiles.

- [ ] **Step 6: Commit the common contract with backend work only after Task 6 passes**

Do not commit this task alone. Keep it as the first part of the cross-backend atomic change completed in Tasks 4-6 so the branch never contains an uncompilable RHI interface.

## Task 4: Implement sampled depth and depth-only rendering on D3D11

**Files:**

- Modify: `Engine/RHI/D3D11/D3D11Rhi.cpp`

- [ ] **Step 1: Create typeless sampled-depth storage**

For `Depth32Float | DepthStencil | Sampled`, use:

```cpp
textureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
```

Continue using `DXGI_FORMAT_D32_FLOAT` storage when the texture is depth-only and never sampled. Reject sampled depth formats other than `Depth32Float`.

- [ ] **Step 2: Bind zero render targets for depth-only passes**

Replace the unconditional one-RTV call with:

```cpp
if (beginInfo.hasColorAttachment)
{
    context_->OMSetRenderTargets(1, &renderTargetView, depthStencilView);
}
else
{
    context_->OMSetRenderTargets(0, nullptr, depthStencilView);
}
```

Reject `colorAttachmentIsSwapchain` when `hasColorAttachment` is false, and clear color only when a color attachment exists.

- [ ] **Step 3: Support vertex-only pipelines and layout validation**

Require a D3D11 vertex module, allow `fragmentShader == nullptr`, pass a null pixel shader into `D3D11PipelineState`, and bind it through `PSSetShader(nullptr, nullptr, 0)`. Copy all layout entries into the pipeline state and assert that each `SetUniformBuffer`, `SetTexture`, and `SetSampler` call exists in the active layout.

- [ ] **Step 4: Remove DSV/SRV hazards before sampling**

Track the active DSV resource. Before `PSSetShaderResources` or `VSSetShaderResources`, if the sampled texture is the active depth texture, execute:

```cpp
context_->OMSetRenderTargets(0, nullptr, nullptr);
activeDepthTexture_ = nullptr;
```

At `BeginRenderPass`, clear matching SRV slots from both VS and PS before binding the same texture as a DSV. Keep this backend-local compatibility behavior out of common renderer code.

- [ ] **Step 5: Build the D3D11 backend**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngine
```

Expected: D3D11 compiles; D3D12 remains the next expected backend failure if it has not yet been updated.

## Task 5: Implement sampled depth, layouts, and transitions on D3D12

**Files:**

- Modify: `Engine/RHI/D3D12/D3D12Rhi.cpp`

- [ ] **Step 1: Create typeless depth storage with typed views**

Use `DXGI_FORMAT_R32_TYPELESS` for the resource, `DXGI_FORMAT_D32_FLOAT` for DSVs, and `DXGI_FORMAT_R32_FLOAT` for the SRV when usage contains both `DepthStencil` and `Sampled`. Keep the optimized clear value typed as `DXGI_FORMAT_D32_FLOAT`.

- [ ] **Step 2: Build the root signature from the declared layout**

Replace the six hard-coded parameters with one root parameter per binding. Use direct CBV root descriptors for uniform buffers and one-descriptor tables for SRVs and samplers. Store this mapping in `D3D12PipelineState`:

```cpp
struct D3D12RootBinding
{
    RhiPipelineResourceKind kind = RhiPipelineResourceKind::UniformBuffer;
    RhiShaderStage stage = RhiShaderStage::Vertex;
    uint32_t slot = 0;
    uint32_t rootParameterIndex = 0;
};
```

`SetUniformBuffer`, `SetTexture`, and `SetSampler` look up the current pipeline's matching record and call the corresponding `SetGraphicsRoot*` method. A missing record is an assertion and no-op in release.

- [ ] **Step 3: Support zero render targets and a null fragment shader**

Set:

```cpp
pipelineDesc.NumRenderTargets = desc.colorAttachmentCount;
pipelineDesc.RTVFormats[0] = desc.colorAttachmentCount == 1 ? ToDxgiFormat(desc.colorFormat) : DXGI_FORMAT_UNKNOWN;
pipelineDesc.PS = fragmentShaderModule != nullptr ? fragmentShaderModule->GetBytecode() : D3D12_SHADER_BYTECODE{};
```

Require the vertex shader and accept a null fragment shader only when `colorAttachmentCount == 0`.

- [ ] **Step 4: Transition depth between write and shader-read states**

At depth pass begin, transition the atlas to `D3D12_RESOURCE_STATE_DEPTH_WRITE`. At render-pass end, transition a stored depth attachment whose usage includes `Sampled` to `D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE`. When a clean cached atlas is imported and only read, `SetTexture` must transition it to pixel-shader resource if its tracked state differs.

- [ ] **Step 5: Build the D3D12 backend**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngine
```

Expected: Windows `VEngine` builds with both D3D11 and D3D12 enabled.

## Task 6: Implement the same RHI contract on Metal and add backend smoke coverage

**Files:**

- Modify: `Engine/RHI/Metal/MetalRhi.mm`
- Create: `Tests/Smoke/RhiVirtualShadowSmokeTests.cpp`
- Create: `CMake/Targets/Tests/RhiVirtualShadowSmokeTests.cmake`
- Modify: `CMake/Targets/Tests.cmake`

- [ ] **Step 1: Implement the Metal mappings**

Use `MTLPixelFormatDepth32Float` for sampled depth. Configure a depth-only `MTLRenderPassDescriptor` without `colorAttachments[0].texture`, set `colorAttachments[0].pixelFormat = MTLPixelFormatInvalid`, and allow a nil fragment function. Copy the common resource layout into `MetalPipelineState` and validate buffer, texture, and sampler indices before calling the native encoder.

- [ ] **Step 2: Write the Windows RHI smoke test**

The test creates a hidden Win32 window, then runs the same function once with `CreateD3D11Device(true)` and once with `CreateD3D12Device(true)`. Its core must exercise this sequence:

```cpp
rhi::RhiTextureDesc atlasDesc = {};
atlasDesc.width = 256;
atlasDesc.height = 256;
atlasDesc.format = rhi::RhiFormat::Depth32Float;
atlasDesc.usage = static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::DepthStencil) |
                                                    static_cast<UInt32>(rhi::RhiTextureUsage::Sampled));
std::unique_ptr<rhi::RhiTexture> atlas = device.CreateTexture(atlasDesc);
passed &= Expect(atlas != nullptr && atlas->GetNativeSampledViewHandle() != nullptr, "Sampled depth atlas should expose an SRV");

rhi::RhiRenderPassBeginInfo depthPass = {};
depthPass.debugName = "VirtualShadowSmokeDepth";
depthPass.hasColorAttachment = false;
depthPass.colorAttachmentIsSwapchain = false;
depthPass.hasDepthAttachment = true;
depthPass.depthAttachment.texture = atlas.get();
depthPass.depthAttachment.loadAction = rhi::RhiLoadAction::Clear;
depthPass.depthAttachment.storeAction = rhi::RhiStoreAction::Store;
passed &= Expect(commandList->BeginRenderPass(*swapchain, depthPass), "Depth-only pass should begin");
commandList->EndRenderPass();
commandList->SetTexture(rhi::RhiShaderStage::Fragment, 1, *atlas);
commandList->SetSampler(rhi::RhiShaderStage::Fragment, 1, *comparisonSampler);
```

Compile tiny in-memory `VSMain` and `PSMain` modules. Create one zero-color vertex-only pipeline for the depth pass and one color pipeline whose layout declares `t1/s1`. The smoke passes when command recording, submission, fence wait, and debug-layer processing complete without an RHI error.

- [ ] **Step 3: Register the smoke test**

Add:

```cmake
function(ve_add_rhi_virtual_shadow_smoke_tests)
    add_executable(VEngineRhiVirtualShadowSmokeTests Tests/Smoke/RhiVirtualShadowSmokeTests.cpp)
    target_link_libraries(VEngineRhiVirtualShadowSmokeTests PRIVATE VEngine)
    ve_configure_target(VEngineRhiVirtualShadowSmokeTests)
    add_test(NAME VEngineRhiVirtualShadowSmokeTests COMMAND $<TARGET_FILE:VEngineRhiVirtualShadowSmokeTests>)
endfunction()
```

Include the file and call the function from `ve_add_tests()`.

- [ ] **Step 4: Run Windows backend verification**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRhiVirtualShadowSmokeTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-tests -C Debug -R VEngineRhiVirtualShadowSmokeTests --output-on-failure
```

Expected: the smoke test passes for D3D11 and D3D12 with no debug-layer DSV/SRV or resource-state error.

- [ ] **Step 5: Commit the complete RHI prerequisite**

```text
git add Engine/RHI Engine/Runtime/Render/Renderer/FrameGraph Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h Tests/Smoke CMake/Targets/Tests.cmake CMake/Targets/Tests/RhiVirtualShadowSmokeTests.cmake
git commit -m "rhi: support virtual shadow depth passes"
```

## Task 7: Add shadow metadata, stable IDs, and revisions to scene proxies

**Files:**

- Modify: `Engine/Runtime/Render/RenderScene.h`
- Modify: `Engine/Runtime/Render/RenderScene.cpp`
- Modify: `Engine/Runtime/Scene/MeshRenderComponent.h`
- Modify: `Engine/Runtime/Scene/MeshRenderComponent.cpp`
- Modify: `Engine/Runtime/Scene/LightComponent.h`
- Modify: `Engine/Runtime/Scene/LightComponent.cpp`
- Modify: `Engine/Runtime/Scene/SceneSerialization.cpp`
- Modify: `Tests/Unit/ResourceRenderTests.cpp`

- [ ] **Step 1: Write failing proxy propagation tests**

Add a test that constructs `RTRenderItem` and `RTLight` directly and checks:

```cpp
ve::RTRenderItemInitParam itemParam = {};
itemParam.renderItemID = 42;
itemParam.castShadows = true;
itemParam.receiveShadows = false;
itemParam.revision = 7;
ve::RTRenderItem item(itemParam);
passed &= Expect(item.GetRenderItemID() == 42, "Render item ID should persist");
passed &= Expect(item.CastShadows(), "Caster flag should persist");
passed &= Expect(!item.ReceiveShadows(), "Receiver flag should persist");
passed &= Expect(item.GetRevision() == 7, "Render item revision should persist");
```

Also verify `RTLight` retains `shadowDistance`, `depthBias`, `normalBias`, and `shadowRevision`.

- [ ] **Step 2: Run the resource-render test and verify failure**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineResourceRenderTests
```

Expected: compilation fails on the new fields and getters.

- [ ] **Step 3: Extend render-item data**

Add `Shadows = 1u << 4` and `Revision = 1u << 5` dirty flags and these init/update fields:

```cpp
UInt64 renderItemID = 0;
bool castShadows = true;
bool receiveShadows = true;
UInt64 revision = 1;
```

Expose `GetRenderItemID()`, `CastShadows()`, `ReceiveShadows()`, and `GetRevision()` on `RTRenderItem`. Every mesh, bounds, transform, cast-shadow, or receive-shadow change increments the component revision before submitting an update; material-only color changes do not increment the shadow revision.

- [ ] **Step 4: Generate component-lifetime stable IDs**

In `MeshRenderComponent.cpp`, use a process-local atomic counter:

```cpp
namespace
{
    std::atomic<UInt64> nextRenderItemID{1};
}

MeshRenderComponent::MeshRenderComponent(Scene& scene, GameObject& owner)
    : Component(scene, owner)
    , renderItemID_(nextRenderItemID.fetch_add(1, std::memory_order_relaxed))
    , rtRenderItem_(nullptr)
{
    BuildRenderItem();
    RegisterTransformChangedCallback();
}
```

Serialize `castShadows` and `receiveShadows`, defaulting both to true when fields are absent. The runtime-generated ID and revision are not serialized.

- [ ] **Step 5: Extend directional-light shadow configuration**

Add component defaults and proxy fields:

```cpp
Float32 shadowDistance_ = 200.0f;
Float32 depthBias_ = 0.001f;
Float32 normalBias_ = 0.05f;
UInt64 shadowRevision_ = 1;
```

Expose getters/setters, serialize the three tunable floats, and increment `shadowRevision_` when cast-shadows, distance, bias, normal bias, or transform changes. The view cache compares light direction separately so a direction change triggers full invalidation.

- [ ] **Step 6: Run the focused tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineResourceRenderTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-tests -C Debug -R VEngineResourceRenderTests --output-on-failure
```

Expected: `VEngineResourceRenderTests` passes.

- [ ] **Step 7: Commit scene shadow metadata**

```text
git add Engine/Runtime/Render/RenderScene.h Engine/Runtime/Render/RenderScene.cpp Engine/Runtime/Scene/MeshRenderComponent.h Engine/Runtime/Scene/MeshRenderComponent.cpp Engine/Runtime/Scene/LightComponent.h Engine/Runtime/Scene/LightComponent.cpp Engine/Runtime/Scene/SceneSerialization.cpp Tests/Unit/ResourceRenderTests.cpp
git commit -m "scene: expose virtual shadow metadata"
```

## Task 8: Introduce persistent, isolated render-view state

**Files:**

- Create: `Engine/Runtime/Render/RenderViewState.h`
- Create: `Engine/Runtime/Render/RenderViewState.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.h`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.cpp`
- Modify: `Editor/Core/Editor.h`
- Modify: `Editor/Core/Editor.cpp`
- Modify: `Editor/Panels/SceneViewPanel/SceneViewPanel.h`
- Modify: `Editor/Panels/SceneViewPanel/SceneViewPanel.cpp`
- Modify: `Editor/Panels/GameViewPanel/GameViewPanel.h`
- Modify: `Editor/Panels/GameViewPanel/GameViewPanel.cpp`
- Modify: `Engine/Runtime/Scene/SceneSystem.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Define Scene Thread and Render Thread view-state contracts**

Use:

```cpp
struct RenderViewStateDesc
{
    std::string name = "RenderView";
    UInt32 virtualShadowAtlasExtent = 4096;
};

class RTRenderViewState final : public NonCopyable
{
public:
    explicit RTRenderViewState(RenderViewStateDesc desc);
    [[nodiscard]] const RenderViewStateDesc& GetDesc() const noexcept;
    [[nodiscard]] VirtualShadowViewCache& GetVirtualShadowCache() noexcept;
private:
    RenderViewStateDesc desc_;
    std::unique_ptr<VirtualShadowViewCache> virtualShadowCache_;
};

class RenderViewState final : public NonCopyable
{
public:
    explicit RenderViewState(RenderViewStateDesc desc);
    [[nodiscard]] std::shared_ptr<RTRenderViewState> GetRTRenderViewState() const noexcept;
    void RequestCameraCut() noexcept;
private:
    std::shared_ptr<RTRenderViewState> rtViewState_;
};
```

The cache type may remain forward-declared until Task 12; initialize it lazily in `GetVirtualShadowCache()`. `RequestCameraCut()` increments an atomic camera cut revision in the RT proxy. `VirtualShadowViewCache::PrepareFrame` consumes a changed cut revision by clearing request-priority history while retaining clean pages reachable through stable absolute keys.

- [ ] **Step 2: Carry the state through renderer input and renderer data**

Add `std::shared_ptr<RTRenderViewState> viewState;` to `BaseRendererInitParam` and `RendererData`. Move it into `rendererData_` in the constructor. Reject a scene renderer invocation with a scene and camera but no view state.

- [ ] **Step 3: Give Editor views independent owners**

Construct these once in panel constructors:

```cpp
sceneViewState_ = std::make_shared<RenderViewState>(RenderViewStateDesc{"EditorSceneView", 2048});
gameViewState_ = std::make_shared<RenderViewState>(RenderViewStateDesc{"EditorGameView", 4096});
```

Expose `GetRenderViewState()` from each panel. Add both RT proxies to `EditorFrameRenderViews`, then assign them in `AddSceneViewRenderer` and `AddGameViewRenderer`. Continue creating the Scene View camera snapshot per frame; its pointer is not the view identity.

Call `RequestCameraCut()` when the Scene View executes an explicit focus/reset command, when the active Game camera proxy changes, and when entering or leaving Play mode. Ordinary Scene View navigation must not request a cut.

- [ ] **Step 4: Give Player its own owner**

Add `std::shared_ptr<RenderViewState> playerViewState` to `SceneSystemImpl`, construct it with `{"PlayerView", 4096}` during initialization, and pass its RT proxy from `CreatePlayerFramePipeline`. Reset it after the final render-thread frame has drained during `SceneSystem::Shutdown`.

Call `RequestCameraCut()` after loading a new active scene or changing the active Player camera. The submitted `FrameRenderPipeline` retains the RT view state in its renderer init data; `FrameContext` already retains the submitted pipeline until its fence completes, so the atlas cannot be destroyed while its last GPU submission is in flight.

- [ ] **Step 5: Build and verify two editor views still render**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineEditor VEnginePlayer
```

Expected: Editor and Player build; rendering is still unshadowed and Scene/Game view state pointers differ.

- [ ] **Step 6: Commit view ownership**

```text
git add Engine/Runtime/Render/RenderViewState.h Engine/Runtime/Render/RenderViewState.cpp Engine/Runtime/Render/Renderer Engine/Runtime/Render/RenderFramePipeline.h Engine/Runtime/Render/RenderFramePipeline.cpp Editor/Core/Editor.h Editor/Core/Editor.cpp Editor/Panels/SceneViewPanel Editor/Panels/GameViewPanel Engine/Runtime/Scene/SceneSystem.cpp CMake/Targets/Engine.cmake
git commit -m "render: add isolated persistent view state"
```

## Task 9: Implement virtual page keys and the GPU resident hash table

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageTable.cpp`
- Create: `Tests/Unit/VirtualShadowTests.cpp`
- Create: `CMake/Targets/Tests/VirtualShadowTests.cmake`
- Modify: `CMake/Targets/Tests.cmake`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Register a failing virtual-shadow test target**

Create the CMake target with the same shape as `VEngineMathTests`, name it `VEngineVirtualShadowTests`, link `VEngine`, include it from `CMake/Targets/Tests.cmake`, and call it from `ve_add_tests()`.

- [ ] **Step 2: Write key and hash-table tests**

Use these exact cases:

```cpp
const ve::VirtualShadowPageKey key = ve::VirtualShadowPageKey::Create(-123, 456, 2, -17);
passed &= Expect(key.GetPageX() == -123 && key.GetPageY() == 456, "Page XY should round-trip");
passed &= Expect(key.GetClipmapLevel() == 2 && key.GetDepthEpoch() == -17, "Level and depth epoch should round-trip");

ve::VirtualShadowPageTable table;
passed &= Expect(table.Insert(key, 19), "Resident mapping should insert");
passed &= Expect(table.Find(key).value_or(ve::InvalidVirtualShadowPhysicalPage) == 19, "Resident mapping should resolve");
passed &= Expect(!table.Find(ve::VirtualShadowPageKey::Create(9, 9, 0, 0)).has_value(), "Missing key should remain missing");
```

Add a deterministic collision test, a 1024-entry load test, and a probe-limit test that verifies the 17th colliding candidate is reported missing.

- [ ] **Step 3: Run the target and verify failure**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests
```

Expected: compilation fails because the virtual-shadow types do not exist.

- [ ] **Step 4: Define exact key packing and configuration**

Implement:

```cpp
struct VirtualShadowPageKey
{
    UInt32 key0 = 0xFFFFFFFFu;
    UInt32 key1 = 0xFFFFFFFFu;
    [[nodiscard]] static VirtualShadowPageKey Create(Int32 pageX, Int32 pageY, UInt32 clipmapLevel, Int32 depthEpoch) noexcept;
    [[nodiscard]] bool IsValid() const noexcept;
    [[nodiscard]] Int32 GetPageX() const noexcept;
    [[nodiscard]] Int32 GetPageY() const noexcept;
    [[nodiscard]] UInt32 GetClipmapLevel() const noexcept;
    [[nodiscard]] Int32 GetDepthEpoch() const noexcept;
    [[nodiscard]] bool operator==(const VirtualShadowPageKey&) const noexcept = default;
};
```

Pack signed 16-bit X/Y into `key0`, level into low eight bits of `key1`, and signed 24-bit depth epoch into the remaining bits. Reject level above three and coordinates outside representable ranges before creating a valid key.

- [ ] **Step 5: Implement the bounded table**

Use 2048 `VirtualShadowGpuPageEntry` records, a maximum of 16 linear probes, and no deletion tombstones because the table is rebuilt for every upload:

```cpp
struct alignas(16) VirtualShadowGpuPageEntry
{
    UInt32 key0 = 0xFFFFFFFFu;
    UInt32 key1 = 0xFFFFFFFFu;
    UInt32 physicalPageIndex = InvalidVirtualShadowPhysicalPage;
    UInt32 flags = 0;
};
static_assert(sizeof(VirtualShadowGpuPageEntry) == 16);
```

Use the same hash in C++ and HLSL: `key0 * 0x9E3779B1u ^ key1 * 0x85EBCA77u`, followed by xor-shift `hash ^= hash >> 16`.

- [ ] **Step 6: Run the focused tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-tests -C Debug -R VEngineVirtualShadowTests --output-on-failure
```

Expected: key, collision, load, missing, and probe-limit tests pass.

- [ ] **Step 7: Commit address translation primitives**

```text
git add Engine/Runtime/Render/VirtualShadow Tests/Unit/VirtualShadowTests.cpp CMake/Targets/Tests/VirtualShadowTests.cmake CMake/Targets/Tests.cmake CMake/Targets/Engine.cmake
git commit -m "render: add virtual shadow page table"
```

## Task 10: Implement physical-page allocation, pinning, and LRU

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.cpp`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Write failing page-cache tests**

Cover free allocation, cache hit, pin protection, oldest-unpinned eviction, dirty-to-clean transition, and overflow:

```cpp
ve::VirtualShadowPageCache cache(2);
cache.BeginFrame(10);
const auto a = cache.Request(ve::VirtualShadowPageRequest{MakeKey(0), 100});
const auto b = cache.Request(ve::VirtualShadowPageRequest{MakeKey(1), 90});
passed &= Expect(a.has_value() && b.has_value(), "Two free physical pages should allocate");
cache.MarkRendered({MakeKey(0), MakeKey(1)});

cache.BeginFrame(11);
cache.Request(ve::VirtualShadowPageRequest{MakeKey(0), 100});
const auto c = cache.Request(ve::VirtualShadowPageRequest{MakeKey(2), 80});
passed &= Expect(c.has_value(), "Unpinned page should be evicted");
passed &= Expect(cache.Contains(MakeKey(0)), "Pinned page should survive eviction");
passed &= Expect(!cache.Contains(MakeKey(1)), "Oldest unpinned page should be evicted");
```

- [ ] **Step 2: Run and verify failure**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests
```

Expected: compilation fails because `VirtualShadowPageCache` is absent.

- [ ] **Step 3: Implement the cache state machine**

Use:

```cpp
enum class VirtualShadowPhysicalPageState { Free, ResidentClean, ResidentDirty };

struct VirtualShadowPhysicalPage
{
    VirtualShadowPhysicalPageState state = VirtualShadowPhysicalPageState::Free;
    VirtualShadowPageKey key;
    UInt64 lastUsedFrame = 0;
    UInt32 generation = 0;
    bool pinnedThisFrame = false;
};
```

`BeginFrame` clears every pin. `ResolveRequests` sorts by descending priority, hits existing pages first, then uses the free list, then evicts the minimum `(lastUsedFrame, physicalPageIndex)` among unpinned pages. New pages are dirty and excluded from `BuildResidentPageTable()` until `MarkRendered` succeeds.

- [ ] **Step 4: Add pressure and isolation tests**

Request four keys from a two-page cache and assert two allocations plus two missing requests. Construct two caches, request disjoint keys, invalidate one cache, and assert the second cache's resident count and mappings are unchanged.

- [ ] **Step 5: Run tests and commit**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-tests -C Debug -R VEngineVirtualShadowTests --output-on-failure
```

Expected: all cache tests pass.

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.h Engine/Runtime/Render/VirtualShadow/VirtualShadowPageCache.cpp Tests/Unit/VirtualShadowTests.cpp CMake/Targets/Engine.cmake
git commit -m "render: add virtual shadow page cache"
```

## Task 11: Build clipmaps and receiver page requests

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.cpp`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.cpp`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Write failing clipmap quantization tests**

Verify a camera move smaller than one page leaves the origin unchanged, crossing one page changes X by exactly one, crossing the depth step changes `depthEpoch`, and all four levels use radii `D/8`, `D/4`, `D/2`, `D`.

```cpp
const ve::VirtualShadowClipmapSet first = ve::BuildVirtualShadowClipmaps(cameraAt(10.0f, 0.0f, 5.0f), lightDirection, 200.0f);
const ve::VirtualShadowClipmapSet subPage = ve::BuildVirtualShadowClipmaps(cameraAt(10.1f, 0.0f, 5.0f), lightDirection, 200.0f);
passed &= Expect(first.levels[0].originPageX == subPage.levels[0].originPageX, "Sub-page motion should preserve origin");
passed &= Expect(first.levels[3].worldRadius == 200.0f, "Last clipmap should cover the shadow distance");
```

- [ ] **Step 2: Implement stable clipmap data**

Define four levels with nominal resolution 16384, 128 pages per axis, and page-world size `2 * radius / 128`. Build an orthonormal light basis by choosing UnitY as helper unless `abs(dot(direction, UnitY)) > 0.99`, then use UnitX. Quantize X/Y with `floor(lightCoordinate / pageWorldSize)` and depth using a fixed step of `shadowDistance / 64`.

- [ ] **Step 3: Write failing receiver request tests**

Create perspective and orthographic camera inputs with one visible receiving AABB and one culled AABB. Assert requests are non-empty, deduplicated, use only levels 0-3, and disappear when `receiveShadows` is false.

- [ ] **Step 4: Implement request generation**

Use this data-only input so tests do not require live `GameObject` state:

```cpp
struct VirtualShadowReceiver
{
    UInt64 renderItemID = 0;
    Aabb worldBounds;
    bool receiveShadows = true;
};

struct VirtualShadowRequestBuildInput
{
    Matrix44 viewProjection = Matrix44::Identity();
    Matrix44 cameraLocalToWorld = Matrix44::Identity();
    VirtualShadowClipmapSet clipmaps;
    std::span<const VirtualShadowReceiver> receivers;
};
```

Frustum-cull each receiver, transform its eight corners into light space, intersect XY with each eligible clipmap square, convert min/max to absolute page coordinates, clamp to that level's 128-by-128 working square, and deduplicate in an unordered map. Set priority to `(3 - level) << 28` plus clamped screen-coverage and inverse-distance terms.

- [ ] **Step 5: Run tests and commit**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-tests -C Debug -R VEngineVirtualShadowTests --output-on-failure
```

Expected: clipmap and request tests pass for both projection modes.

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.h Engine/Runtime/Render/VirtualShadow/VirtualShadowClipmap.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.h Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestBuilder.cpp Tests/Unit/VirtualShadowTests.cpp CMake/Targets/Engine.cmake
git commit -m "render: build virtual shadow page requests"
```

## Task 12: Track caster invalidation and build dirty-page caster lists

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowInvalidationTracker.cpp`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- Modify: `Engine/Runtime/Render/RenderViewState.cpp`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Write failing invalidation tests**

Feed snapshots with stable IDs and revisions through three frames: add at A, move from A to B, remove from B. Assert add invalidates A, move invalidates A and B, and remove invalidates B. Add a light-direction change and assert every resident page is dirty. Run the same frames through two trackers and verify consuming one does not alter the other.

- [ ] **Step 2: Implement per-view caster history**

Use:

```cpp
struct VirtualShadowCasterSnapshot
{
    UInt64 renderItemID = 0;
    UInt64 revision = 0;
    Aabb worldBounds;
    bool castShadows = true;
};

struct VirtualShadowTrackedCaster
{
    UInt64 revision = 0;
    Aabb worldBounds;
    UInt64 lastSeenFrame = 0;
};
```

For additions invalidate new bounds; for revision changes invalidate saved and new bounds; after scanning, entries not seen this frame invalidate saved bounds and are erased. Convert bounds to page keys through every clipmap whose volume they overlap.

- [ ] **Step 3: Implement the CPU half of the view cache**

`VirtualShadowViewCache::PrepareFrame` accepts device, frame index, camera, scene, light, and atlas extent. It must:

1. Validate the configuration and locate one shadow-casting directional light.
2. Build world bounds for render items.
3. Build clipmaps and receiver requests.
4. Apply caster invalidation independently for this view.
5. Resolve requests through `VirtualShadowPageCache`.
6. For every dirty page, intersect its gutter-expanded light-space page volume with opaque caster bounds.
7. Produce `VirtualShadowDirtyPageDraw` records containing key, physical slot, page matrix, and caster pointers.
8. Build statistics without touching another view state.

Do not create RHI resources yet; return a disabled but valid frame packet when no suitable light exists.

Treat light-direction changes as full invalidation. Shadow-distance changes rebuild clipmap requests through new stable keys. Depth-bias and normal-bias revisions update only the GPU sampling constants and must not dirty resident pages.

- [ ] **Step 4: Add end-to-end CPU isolation tests**

Construct two `RTRenderViewState` instances with atlas extents 2048 and 4096, prepare them with different cameras against the same proxy snapshots, and assert different clipmap origins, physical capacities 256 and 1024, distinct page mappings, and independent invalidation/statistics.

- [ ] **Step 5: Run tests and commit**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests
CMake/Scripts/WithMsvc.bat ctest --test-dir Build/windows-msvc-tests -C Debug -R VEngineVirtualShadowTests --output-on-failure
```

Expected: invalidation, caster culling, and two-view isolation tests pass.

```text
git add Engine/Runtime/Render/VirtualShadow Engine/Runtime/Render/RenderViewState.cpp Tests/Unit/VirtualShadowTests.cpp CMake/Targets/Engine.cmake
git commit -m "render: prepare per-view virtual shadows"
```

## Task 13: Create the atlas and render dirty physical pages

**Files:**

- Create: `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.h`
- Create: `Engine/Runtime/Render/Renderer/RenderPass/VirtualShadowDepthRenderPass.cpp`
- Create: `Assets/Builtin/Shaders/VirtualShadowClear.hlsl`
- Create: `Assets/Builtin/Shaders/VirtualShadowClear.veshader`
- Create: `Assets/Builtin/Shaders/VirtualShadowClear.veshader.meta`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/MobileRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/MobileRenderer.cpp`
- Modify: `Engine/Runtime/Render/RenderShaderIDs.h`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Create persistent atlas and comparison sampler resources**

On first `PrepareFrame`, or when atlas extent changes, create:

```cpp
rhi::RhiTextureDesc atlasDesc = {};
atlasDesc.width = atlasExtent;
atlasDesc.height = atlasExtent;
atlasDesc.format = rhi::RhiFormat::Depth32Float;
atlasDesc.usage = static_cast<rhi::RhiTextureUsage>(static_cast<UInt32>(rhi::RhiTextureUsage::DepthStencil) |
                                                    static_cast<UInt32>(rhi::RhiTextureUsage::Sampled));
atlasDesc.debugName = viewName.c_str();

rhi::RhiSamplerDesc samplerDesc = {};
samplerDesc.filter = rhi::RhiSamplerFilter::Point;
samplerDesc.addressU = rhi::RhiSamplerAddressMode::Clamp;
samplerDesc.addressV = rhi::RhiSamplerAddressMode::Clamp;
samplerDesc.reductionMode = rhi::RhiSamplerReductionMode::Comparison;
samplerDesc.comparisonFunction = rhi::RhiCompareFunction::LessEqual;
```

If either creation fails, disable VSM for this view and leave forward rendering fully lit.

- [ ] **Step 2: Add the clear shader asset**

Use this HLSL; keep `PSMain` only because the current asset tool emits both stages, while the clear pipeline binds no fragment shader:

```hlsl
struct VSOutput { float4 position : SV_POSITION; };

VSOutput VSMain(uint vertexID : SV_VertexID)
{
    const float2 positions[3] = {float2(-1.0f, -1.0f), float2(-1.0f, 3.0f), float2(3.0f, -1.0f)};
    VSOutput output;
    output.position = float4(positions[vertexID], 1.0f, 1.0f);
    return output;
}

float4 PSMain() : SV_TARGET
{
    return 0.0f;
}
```

Create metadata with a new fixed GUID and an empty material property list.

- [ ] **Step 3: Import the atlas and register the depth-only pass**

Add `FrameGraphTextureHandle virtualShadowAtlas;` to `RendererFrameGraphData`. Import the persistent atlas with `DepthStencil | Sampled`. If dirty pages exist, `VirtualShadowDepthRenderPass` writes it with `Load`; otherwise keep the imported version. Opaque and transparent passes will read the final version in Task 14.

- [ ] **Step 4: Record page-local clear and caster draws**

For each dirty page:

```cpp
const UInt32 pagesPerRow = atlasExtent / VirtualShadowPhysicalPageSize;
const UInt32 slotX = physicalPageIndex % pagesPerRow;
const UInt32 slotY = physicalPageIndex / pagesPerRow;
const rhi::RhiViewport viewport{Float32(slotX * 128), Float32(slotY * 128), 128.0f, 128.0f, 0.0f, 1.0f};
const rhi::RhiScissorRect scissor{Int32(slotX * 128), Int32(slotY * 128), 128, 128};
commandList.SetViewport(viewport);
commandList.SetScissor(scissor);
commandList.SetPipeline(*clearPipeline);
commandList.Draw(3, 0);
```

Then bind the caster pipeline: vertex shader from the caster's `RTShaderResource`, null fragment shader, zero colors, front-face culling, depth write, and configured constant/slope bias. Bind page view-projection at vertex `b1`, object transform at vertex `b2`, and draw opaque casters. Expand the page projection by one texel so the 126-by-126 usable interior has a rendered gutter.

- [ ] **Step 5: Publish only successfully recorded pages**

Call `MarkRendered` only after clear and caster commands for every page have been recorded. Rebuild the 2048-entry resident table after marking; never expose a newly allocated dirty page earlier in the frame.

- [ ] **Step 6: Build and smoke the pass without forward sampling**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineEditor VEnginePlayer VEngineShaderTool
```

Expected: all targets build. With VSM preparation enabled but forward sampling disabled, normal scene color remains unchanged and the atlas pass records only dirty pages.

- [ ] **Step 7: Commit atlas rendering**

```text
git add Engine/Runtime/Render/VirtualShadow Engine/Runtime/Render/Renderer Assets/Builtin/Shaders/VirtualShadowClear.hlsl Assets/Builtin/Shaders/VirtualShadowClear.veshader Assets/Builtin/Shaders/VirtualShadowClear.veshader.meta Engine/Runtime/Render/RenderShaderIDs.h CMake/Targets/Engine.cmake
git commit -m "render: draw virtual shadow atlas pages"
```

## Task 14: Sample virtual shadows in opaque and transparent forward passes

**Files:**

- Modify: `Assets/Builtin/Shaders/BasicMesh.hlsl`
- Modify: `Assets/Builtin/Shaders/BasicMesh.veshader`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`

- [ ] **Step 1: Define one C++/HLSL-compatible upload block**

Keep the page table below the D3D11 64 KiB constant-buffer limit:

```cpp
struct alignas(16) VirtualShadowGpuClipmap
{
    Vector4 lightOriginAndPageWorldSize; // origin X, origin Y, depth center, page-world size
    Vector4 radiusAndDepthRange;         // radius, near depth, far depth, unused
    Int32 originPageX = 0;
    Int32 originPageY = 0;
    Int32 depthEpoch = 0;
    Int32 padding = 0;
};

struct alignas(16) VirtualShadowGpuConstants
{
    Vector4 lightRight;
    Vector4 lightUp;
    Vector4 lightDirection;
    Vector4 atlasAndBias;
    VirtualShadowGpuClipmap clipmaps[4];
    UInt32 enabled = 0;
    UInt32 atlasExtent = 0;
    UInt32 physicalPageSize = 128;
    UInt32 clipmapLevelCount = 4;
    VirtualShadowGpuPageEntry entries[2048];
};
static_assert(sizeof(VirtualShadowGpuConstants) <= 65536);
```

Use `atlasAndBias = {1.0f / atlasExtent, depthBias, normalBias, 126.0f / 128.0f}`. Mirror the three 16-byte clipmap rows exactly in HLSL so page X/Y and depth epoch remain integer values.

- [ ] **Step 2: Declare graph reads in both forward passes**

Add a shadow-atlas handle to each pass data. During setup:

```cpp
if (graphData.virtualShadowAtlas.IsValid())
{
    passData.virtualShadowAtlas = builder.Read(graphData.virtualShadowAtlas);
}
```

During execute, resolve the handle through `FrameGraphPassResources`, upload `VirtualShadowGpuConstants`, bind fragment `b4`, atlas `t1`, and comparison sampler `s1`. Use a disabled zeroed constant block and skip texture/sampler binding when the view cache is disabled.

- [ ] **Step 3: Declare the exact forward resource layout**

Both pipelines declare:

```cpp
const rhi::RhiPipelineResourceBindingDesc bindings[] = {
    {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 0},
    {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 1},
    {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Vertex, 2},
    {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 3},
    {rhi::RhiPipelineResourceKind::UniformBuffer, rhi::RhiShaderStage::Fragment, 4},
    {rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Fragment, 0},
    {rhi::RhiPipelineResourceKind::SampledTexture, rhi::RhiShaderStage::Fragment, 1},
    {rhi::RhiPipelineResourceKind::Sampler, rhi::RhiShaderStage::Fragment, 0},
    {rhi::RhiPipelineResourceKind::Sampler, rhi::RhiShaderStage::Fragment, 1},
};
```

Include a shadow-enabled bit in the pipeline variant only if shader compilation produces a distinct variant; otherwise use runtime `enabled` branching and preserve the existing material pipeline cache key.

- [ ] **Step 4: Pass world position and receiver state to the fragment shader**

Extend `VSOutput` with `float3 worldPosition : TEXCOORD0`. Add `uint receiveShadows` to object constants, keeping the C++ upload block 256-byte aligned. Write world position in `VSMain` and bypass VSM lookup when the flag is zero.

- [ ] **Step 5: Implement bounded page lookup and coarse fallback**

Use the identical hash and at most 16 probes:

```hlsl
uint FindPhysicalPage(uint2 key)
{
    uint hash = key.x * 0x9E3779B1u ^ key.y * 0x85EBCA77u;
    hash ^= hash >> 16;
    [unroll]
    for (uint probe = 0; probe < 16; ++probe)
    {
        VirtualShadowPageEntry entry = virtualShadowPages[(hash + probe) & 2047u];
        if (entry.key0 == 0xFFFFFFFFu && entry.key1 == 0xFFFFFFFFu) return 0xFFFFFFFFu;
        if (entry.key0 == key.x && entry.key1 == key.y && (entry.flags & 1u) != 0u) return entry.physicalPageIndex;
    }
    return 0xFFFFFFFFu;
}
```

Start at the level selected by camera-relative distance and try every coarser level. If all are missing, return visibility 1.0.

- [ ] **Step 6: Implement gutter-safe 3x3 PCF**

Map local coordinates into the 126-by-126 interior offset by one texel. Clamp each tap to `[slotMin + 1.0, slotMax - 1.0]` in texel space before converting to atlas UV. Apply normal bias along the world normal before projection and subtract depth bias from the comparison reference. Multiply only directional diffuse by visibility; leave ambient unchanged.

- [ ] **Step 7: Compile shader artifacts and build both Windows backends**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineShaderTool VEngineEditor VEnginePlayer
Build/windows-msvc-debug/Debug/VEngineShaderTool.exe compile --source Assets/Builtin/Shaders/BasicMesh.hlsl --output Build/Generated/VirtualShadowBasicMesh --name BasicMesh --dxc ThirdParty/DirectXShaderCompiler/Build/Windows64/1.9.2602.17/Tools/x64/dxc.exe --fxc fxc --slang ThirdParty/Slang/slang-2026.12-windows-x86_64/bin/slangc.exe
```

Expected: DXBC, DXIL, Metal, and reflection artifacts are emitted; Editor and Player build.

- [ ] **Step 8: Commit forward shadow sampling**

```text
git add Assets/Builtin/Shaders/BasicMesh.hlsl Assets/Builtin/Shaders/BasicMesh.veshader Engine/Runtime/Render/VirtualShadow Engine/Runtime/Render/Renderer/RenderPass
git commit -m "render: sample virtual shadows in forward passes"
```

## Task 15: Add diagnostics, editor visualization, and graceful degradation

**Files:**

- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowViewCache.cpp`
- Modify: `Editor/Panels/SceneViewPanel/SceneViewPanel.h`
- Modify: `Editor/Panels/SceneViewPanel/SceneViewPanel.cpp`
- Modify: `Editor/Panels/GameViewPanel/GameViewPanel.h`
- Modify: `Editor/Panels/GameViewPanel/GameViewPanel.cpp`
- Modify: `Assets/Builtin/Shaders/BasicMesh.hlsl`

- [ ] **Step 1: Expose per-view statistics safely**

Use an atomic snapshot on `RTRenderViewState`:

```cpp
struct VirtualShadowStatistics
{
    UInt32 requested = 0;
    UInt32 resident = 0;
    UInt32 allocated = 0;
    UInt32 cached = 0;
    UInt32 dirty = 0;
    UInt32 rendered = 0;
    UInt32 evicted = 0;
    UInt32 missing = 0;
    UInt32 casterDraws = 0;
};
```

Publish through a trivially copyable seqlock or individually atomic counters; Scene Thread UI must never read the mutable render-thread cache directly.

- [ ] **Step 2: Add Scene and Game view statistic overlays**

Add a collapsible `Virtual Shadows` section to each view's render popup. Display the nine counters and the view's atlas extent. The labels must include `Scene View` or `Game View`, making isolation visible during the same editor frame.

- [ ] **Step 3: Add shader debug modes**

Add `VirtualShadowDebugMode` values `None`, `ClipmapLevel`, `Residency`, and `VirtualPageAddress` to the view state. Upload the selected value in the constants block. In `BasicMesh.hlsl`, replace final color only when the mode is nonzero: level palette for clipmaps, green/yellow/red for resident/coarse-fallback/missing, and a stable hash color for virtual page address.

- [ ] **Step 4: Add atlas and caster-overlap visualizers**

Publish the atlas sampled-view handle from `RTRenderViewState` through `std::atomic<void*>` and display it in the Scene View render popup as a depth preview. Publish a copied debug snapshot containing caster world AABBs and dirty-page light-space volumes; append their line segments to the existing Scene View gizmo draw list when `Caster Bounds` or `Dirty Page Overlap` is enabled. The snapshot must contain values only and must not expose render-thread vectors or RHI owners to the Scene Thread.

- [ ] **Step 5: Rate-limit degradation warnings**

Log at most once every 120 frames per view for atlas creation failure, pool pressure, invalid bounds, or hash insertion failure. Include the view name and relevant counts. Missing pages remain fully lit after coarse fallback; no warning path may abort scene rendering.

- [ ] **Step 6: Build and manually inspect same-frame isolation**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineEditor
```

Expected: moving only the Scene View camera changes Scene statistics and clipmap visualization while Game View atlas, origins, LRU counts, and statistics stay unchanged.

- [ ] **Step 7: Commit diagnostics**

```text
git add Engine/Runtime/Render/VirtualShadow Editor/Panels/SceneViewPanel Editor/Panels/GameViewPanel Assets/Builtin/Shaders/BasicMesh.hlsl
git commit -m "editor: expose virtual shadow diagnostics"
```

## Task 16: Complete cross-backend acceptance and documentation

**Files:**

- Modify: `Docs/RenderSystemDesign.md`
- Modify: `Docs/DevelopmentPlan.md`
- Modify: `Docs/ShaderToolUsage.md`

- [ ] **Step 1: Run all Windows automated verification**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: all existing tests plus `VEngineVirtualShadowTests` and `VEngineRhiVirtualShadowSmokeTests` pass.

- [ ] **Step 2: Run D3D11 scene acceptance**

Launch `VEngineEditor` with the existing D3D11 backend selection and verify: static scenes converge to nearly zero rendered dirty pages; slow camera motion adds only entering pages; moving a caster removes its old shadow; pool overflow produces missing distant shadows without corruption; closing/reopening views produces no D3D11 debug-layer hazard.

- [ ] **Step 3: Run D3D12 scene acceptance**

Repeat the same scene and camera sequence with D3D12. Verify resident keys and physical counts match D3D11 for the same view, and the D3D12 debug layer reports no invalid depth-write/shader-read transition or descriptor-root mismatch.

- [ ] **Step 4: Run macOS build and Metal acceptance**

On macOS, run the repository's existing configure/build commands for `VEngineMacPlayer` and `VEngineEditor`, then load the same smoke scene. Verify sampled depth creation, depth-only encoding, nil fragment functions, comparison sampling, view destruction, and visually equivalent coverage.

- [ ] **Step 5: Update canonical documentation**

Document these fixed first-stage limits in `Docs/RenderSystemDesign.md`: one directional light, four levels, nominal 16K per level, 128 physical page size, one-texel gutter, fixed per-view committed atlas, CPU requests/LRU/culling, 2048-entry hash table, 16 probes, D3D11 parity, and fully isolated view caches. Mark the milestone complete in `Docs/DevelopmentPlan.md` only after Steps 1-4 pass. Add the two shadow shader compilation examples to `Docs/ShaderToolUsage.md`.

- [ ] **Step 6: Verify the final diff**

Run:

```text
git diff --check
git status --short
```

Expected: no whitespace errors; status contains only intentional VSM, test, shader, CMake, and documentation changes.

- [ ] **Step 7: Commit the verified feature**

```text
git add Docs Engine Editor Assets CMake Tests
git commit -m "render: complete per-view virtual shadow maps"
```

## Completion checklist

- [ ] D3D11 and D3D12 render equivalent dynamic directional shadows.
- [ ] Scene View and Game View own different atlases, mappings, history, LRU, and statistics.
- [ ] Static scenes reuse clean pages instead of redrawing the working set.
- [ ] Caster movement invalidates both old and new page coverage.
- [ ] Missing fine pages fall back to coarser levels and then fully lit.
- [ ] Three-by-three PCF never samples an adjacent physical slot.
- [ ] View resize, closure, target replacement, and shutdown are fence-safe.
- [ ] Metal implements and passes the common sampled-depth and depth-only contracts.
- [ ] Automated CPU and RHI smoke tests pass.
- [ ] Canonical render and development documentation reflects the implemented limits.
