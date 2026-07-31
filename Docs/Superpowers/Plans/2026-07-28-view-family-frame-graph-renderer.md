# View-Family Frame-Graph Renderer Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make one frame-local renderer consume one `RenderViewFamily`, own the frame's single `FrameGraph`, and schedule generic depth, VSM, scene, extension, and product-output work through declared resource dependencies.

**Architecture:** `FrameRenderPipeline` remains the frame-local Editor/Player environment selector and constructs one frame-local `StandaloneRenderer` or `MobileRenderer`. The renderer owns one family-oriented FrameGraph, while `RenderSystem` retains every cross-frame cache and acceleration structure, including `VirtualShadowManager`. VSM GPU work becomes focused graph passes; renderer-internal recording APIs become `void + assert`, while FrameGraph validation and external RHI/device/swapchain/submission boundaries retain result values.

**Tech Stack:** C++20, VEngine RenderSystem and common RHI, FrameGraph, embedded HLSL, D3D11, D3D12, Metal compatibility path, Dear ImGui, CMake, CTest.

---

## File Structure

### New files

- `Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.h`
- `Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.cpp`
  - Generic per-view opaque depth pass reused by VSM request generation and scene rendering.
- `Engine/Runtime/Render/Renderer/RenderPass/SwapchainOutputRenderPass.h`
- `Engine/Runtime/Render/Renderer/RenderPass/SwapchainOutputRenderPass.cpp`
  - Frame-local Editor overlay and Player view-color copy output nodes.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp`
  - Prepared family/view graph data, persistent-resource imports, and focused VSM pass registration.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.cpp`
  - Clear-scene, clear-view, mark-request, and compact-request graph passes.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.cpp`
  - Per-view/per-level resident-hit resolution and deterministic allocation graph passes.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.cpp`
  - Physical-page clear, caster raster, rendered marking, scene finalization, and statistics-copy graph passes.
- `Tests/Render/RendererFrameGraphSmokeTests.cpp`
- `CMake/Targets/Tests/RendererFrameGraphSmokeTests.cmake`
  - Family data, graph API, pass dependency, queue-building, and lightweight topology tests.
- `Tests/Render/VirtualShadowFrameGraphSmokeTests.cpp`
- `CMake/Targets/Tests/VirtualShadowFrameGraphSmokeTests.cmake`
  - Final names for the existing real-backend VSM smoke after legacy GPU-pipeline removal.

### Modified files

- `Engine/Runtime/Render/RenderFramePipeline.h`
- `Engine/Runtime/Render/RenderFramePipeline.cpp`
  - Reduce Editor/Player pipeline execution to command-list lifetime, renderer construction, and invocation.
- `Engine/Runtime/Render/RenderSystem.cpp`
  - Adapt to `void RenderFrame`, retain submit/present recovery, and preserve VSM transaction commit/abort.
- `Engine/Runtime/Scene/SceneSystem.cpp`
  - Construct the Player renderer input as a one-view family.
- `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
  - Own a family, build family data, own the one FrameGraph, import resources, compile, and execute.
- `Engine/Runtime/Render/Renderer/StandaloneRenderer.h`
- `Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp`
- `Engine/Runtime/Render/Renderer/MobileRenderer.h`
- `Engine/Runtime/Render/Renderer/MobileRenderer.cpp`
- `Engine/Runtime/Render/Renderer/RendererFactory.h`
- `Engine/Runtime/Render/Renderer/RendererFactory.cpp`
  - Accept family renderer inputs and register topology-specific nodes without persistent renderer state.
- `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h`
- `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.cpp`
  - Keep `RendererData` name, add indexed view data, add a view-pass interface, and expose family data to pass execution.
- `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h`
- `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h`
- `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
  - Register one node per view and use `void + assert` recording.
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h`
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.cpp`
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h`
  - Add family context, void callbacks, and explicit copy source/destination dependencies.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
  - Retain cross-frame state, prepare immutable family data, register graph work, and remove `PreRenderShadowStep`.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
  - Remove after its shaders and focused recording logic have moved to graph pass files.
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.h`
- `Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.cpp`
  - Adapt sampling snapshots and prepared data to indexed family views.
- `Editor/Core/Editor.cpp`
  - Build one Editor family and associate Grid/Gizmo extensions with the SceneView index.
- `Editor/RenderPass/SceneGridRenderPass.h`
- `Editor/RenderPass/SceneGridRenderPass.cpp`
- `Editor/RenderPass/EditorGizmoRenderPass.h`
- `Editor/RenderPass/EditorGizmoRenderPass.cpp`
  - Implement the indexed view-pass contract and `void + assert` recording.
- `Tests/Unit/VirtualShadowTests.cpp`
- `Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp` (renamed during Task 9)
  - Replace legacy direct-pipeline assertions with graph dependency and real-backend execution checks.
- `CMake/Targets/Engine.cmake`
- `CMake/Targets/Tests.cmake`
  - Register new pass sources/tests and remove deleted legacy sources.
- `Docs/ArchitectureOverview.md`
- `Docs/RenderSystemDesign.md`
  - Record the final ownership, one-family renderer, one-graph topology, and fail-fast boundary.

## Task 1: Extend FrameGraph For Void Passes And Copy Dependencies

**Files:**

- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.cpp`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.cpp`
- Modify: `Editor/RenderPass/SceneGridRenderPass.cpp`
- Modify: `Editor/RenderPass/EditorGizmoRenderPass.cpp`
- Create: `Tests/Render/RendererFrameGraphSmokeTests.cpp`
- Create: `CMake/Targets/Tests/RendererFrameGraphSmokeTests.cmake`
- Modify: `CMake/Targets/Tests.cmake`

- [ ] **Step 1: Add failing API-contract tests**

Create `RendererFrameGraphSmokeTests.cpp` with compile-time checks for the new accesses and void callbacks:

```cpp
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h"
#include "Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h"

#include <type_traits>

static_assert(ve::FrameGraphTextureAccess::CopySource != ve::FrameGraphTextureAccess::CopyDestination);
static_assert(ve::FrameGraphBufferAccess::CopySource != ve::FrameGraphBufferAccess::CopyDestination);

namespace
{
    struct EmptyPassData
    {
    };

    void RegisterVoidPass(ve::FrameGraph& graph)
    {
        graph.AddComputePass<EmptyPassData>(
            "VoidCompute",
            [](ve::FrameGraphBuilder&, EmptyPassData&) {},
            [](const EmptyPassData&, const ve::FrameGraphPassResources&, ve::RenderPassContext&) {});
    }
}
```

Register `VEngineRendererFrameGraphSmokeTests` in the new CMake file and call it from `CMake/Targets/Tests.cmake`.

- [ ] **Step 2: Run the focused build and verify the contract is missing**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
```

Expected: compilation fails because copy accesses and void execute callbacks are unsupported.

- [ ] **Step 3: Add explicit copy access APIs**

Extend the enums:

```cpp
enum class FrameGraphTextureAccess
{
    ColorAttachment,
    DepthAttachment,
    ShaderRead,
    CopySource,
    CopyDestination,
};

enum class FrameGraphBufferAccess
{
    ShaderRead,
    ShaderReadWrite,
    CopySource,
    CopyDestination,
};
```

Add builder methods:

```cpp
[[nodiscard]] FrameGraphTextureHandle ReadCopySource(FrameGraphTextureHandle handle);
[[nodiscard]] FrameGraphTextureHandle WriteCopyDestination(FrameGraphTextureHandle handle);
[[nodiscard]] FrameGraphBufferHandle ReadCopySource(FrameGraphBufferHandle handle);
[[nodiscard]] FrameGraphBufferHandle WriteCopyDestination(FrameGraphBufferHandle handle);
```

Each read calls the existing version-preserving read path with `CopySource`; each write creates the next logical version
with `CopyDestination`. Update declaration validation so copy access is legal on matching texture/buffer resources and
still participates in producer/consumer edges.

- [ ] **Step 4: Convert graph setup and execute callbacks to void**

Use:

```cpp
using GraphSetupFunction = std::function<void(FrameGraph&)>;
using ExecuteFunction = std::function<void(const FrameGraphPassResources&, RenderPassContext&)>;

void Setup(GraphSetupFunction setupFunction);
```

Update `AddRasterPass` and `AddComputePass` to require either of these signatures:

```cpp
void(const PassData&, const FrameGraphPassResources&, RenderPassContext&)
void(const PassData&, RenderPassContext&)
```

Keep:

```cpp
[[nodiscard]] Error Compile();
[[nodiscard]] ErrorCode Execute();
```

`Execute` continues reporting failures from transient acquisition and automatic native render-pass begin/end. User
callbacks no longer return values.

Mechanically migrate every existing graph call site in the same commit so the engine remains buildable:

- remove `setupResult` handling because `FrameGraph::Setup` is now declarative `void`;
- inside the temporary BaseRenderer setup callback, evaluate `ImportRenderTargets`, log its `ErrorCode`, and
  always-on assert success;
- change Opaque, Transparent, Grid, Gizmo, and swapchain-overlay execute lambdas to call their existing
  `ErrorCode`-returning helpers, log the pass name on failure, and always-on assert success;
- do not change those private helper signatures yet—Task 9 removes their remaining internal result propagation after
  the family/VSM migration is complete.

- [ ] **Step 5: Add graph compile tests for copy versions**

Add a test that creates graph-owned resources, registers copy nodes, exports the destination versions, and compiles:

```cpp
struct CopyPassData
{
    ve::FrameGraphTextureHandle source;
    ve::FrameGraphTextureHandle destination;
    ve::FrameGraphBufferHandle readback;
};

struct ProduceTexturePassData
{
    ve::FrameGraphTextureHandle output;
};

bool TestCopyDependenciesCompile()
{
    ve::FrameRenderPipelineData frameData = {};
    ve::RendererData rendererData = {};
    ve::FrameGraph graph({frameData, rendererData});
    ve::FrameGraphTextureHandle destination;
    ve::FrameGraphBufferHandle readback;
    TestBuffer readbackBacking(64);

    graph.Setup(
        [&](ve::FrameGraph& setupGraph)
        {
            ve::FrameGraphTextureDesc textureDesc = {};
            textureDesc.width = 16;
            textureDesc.height = 16;
            textureDesc.usage = static_cast<ve::rhi::RhiTextureUsage>(
                static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::Sampled) |
                static_cast<ve::UInt32>(ve::rhi::RhiTextureUsage::RenderTarget));
            auto source = setupGraph.CreateTexture("CopySource", textureDesc);
            destination = setupGraph.CreateTexture("CopyDestination", textureDesc);
            readback = setupGraph.ImportBuffer("Readback", {&readbackBacking});

            setupGraph.AddRasterPass<ProduceTexturePassData>(
                "ProduceCopySource",
                [&](ve::FrameGraphBuilder& builder, ProduceTexturePassData& data)
                {
                    data.output = builder.WriteColorAttachment(
                        source, ve::rhi::RhiLoadAction::Clear);
                    source = data.output;
                },
                [](const ProduceTexturePassData&,
                   const ve::FrameGraphPassResources&,
                   ve::RenderPassContext&) {});

            setupGraph.AddComputePass<CopyPassData>(
                "Copy",
                [&](ve::FrameGraphBuilder& builder, CopyPassData& data)
                {
                    data.source = builder.ReadCopySource(source);
                    data.destination = builder.WriteCopyDestination(destination);
                    data.readback = builder.WriteCopyDestination(readback);
                    destination = data.destination;
                    readback = data.readback;
                },
                [](const CopyPassData&, const ve::FrameGraphPassResources&, ve::RenderPassContext&) {});
            setupGraph.Export(destination);
            setupGraph.Export(readback);
        });

    return graph.Compile().IsOk();
}
```

Define the backing test object before `TestCopyDependenciesCompile` in the test file:

```cpp
class TestBuffer final : public ve::rhi::RhiBuffer
{
public:
    explicit TestBuffer(uint64_t size) noexcept
        : size_(size)
    {
    }

    [[nodiscard]] uint64_t GetSize() const noexcept override
    {
        return size_;
    }

private:
    uint64_t size_ = 0;
};
```

Add an executable entry point that returns zero only when `TestCopyDependenciesCompile()` succeeds:

```cpp
int main()
{
    return TestCopyDependenciesCompile() ? 0 : 1;
}
```

- [ ] **Step 6: Run the focused test**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineRendererFrameGraphSmokeTests --output-on-failure
```

Expected: build and test pass.

- [ ] **Step 7: Commit**

```text
git add Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.cpp Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h Engine/Runtime/Render/Renderer/BaseRenderer.cpp Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp Engine/Runtime/Render/RenderFramePipeline.cpp Editor/RenderPass/SceneGridRenderPass.cpp Editor/RenderPass/EditorGizmoRenderPass.cpp Tests/Render/RendererFrameGraphSmokeTests.cpp CMake/Targets/Tests/RendererFrameGraphSmokeTests.cmake CMake/Targets/Tests.cmake
git commit -m "render: extend frame graph pass contracts"
```

## Task 2: Make RendererData Family-Oriented

**Files:**

- Modify: `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`
- Modify: `Tests/Render/RendererFrameGraphSmokeTests.cpp`

- [ ] **Step 1: Add failing family-data tests**

Add tests that preserve the `RendererData` name and require indexed view data:

```cpp
static_assert(std::is_same_v<decltype(ve::RendererData::scene), std::shared_ptr<ve::RTScene>>);
static_assert(std::is_same_v<decltype(ve::RendererData::views), std::vector<ve::RendererViewData>>);

bool TestRendererDataKeepsIndependentViews()
{
    ve::RendererData data;
    data.views.resize(2);
    data.views[0].view.fillMode = ve::rhi::RhiFillMode::Solid;
    data.views[1].view.fillMode = ve::rhi::RhiFillMode::Wireframe;
    return data.views[0].view.fillMode != data.views[1].view.fillMode;
}
```

Also require one family graph-data element per view:

```cpp
ve::RendererFrameGraphData graphData;
graphData.views.resize(2);
return graphData.views.size() == 2;
```

Append both runtime checks to the smoke executable's `main` success accumulator.

- [ ] **Step 2: Run the focused build and verify it fails**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
```

Expected: compilation fails because `RendererViewData` and `RendererData::views` do not exist.

- [ ] **Step 3: Add family data beside the migration-only single-view fields**

Define:

```cpp
struct RendererViewData
{
    RenderView view;
    VirtualShadowSamplingSnapshot virtualShadowSampling;
    std::vector<std::shared_ptr<RTRenderItem>> transparentItems;
};

struct RendererData
{
    std::shared_ptr<RTScene> scene;
    std::vector<std::shared_ptr<RTRenderItem>> opaqueItems;
    std::vector<RendererViewData> views;

    // Migration-only fields removed in Task 3.
    std::shared_ptr<RTCamera> resolvedCamera;
    std::shared_ptr<RTRenderViewState> viewState;
    std::shared_ptr<const VirtualShadowViewResult> virtualShadowResult;
    VirtualShadowSamplingSnapshot virtualShadowSampling;
    std::vector<std::shared_ptr<RTRenderItem>> transparentItems;
};

struct RendererViewFrameGraphData
{
    FrameGraphTextureHandle color;
    FrameGraphTextureHandle depth;
    FrameGraphBufferHandle virtualShadowPageTable;
    VirtualShadowSamplingSnapshot virtualShadowSampling;
};

struct RendererFrameGraphData
{
    std::vector<RendererViewFrameGraphData> views;
    FrameGraphTextureHandle virtualShadowAtlas;
    FrameGraphBufferHandle virtualShadowPhysicalPages;
    FrameGraphBufferHandle virtualShadowStatistics;
    FrameGraphTextureHandle swapchainColor;

    // Migration-only fields removed in Task 3.
    FrameGraphTextureHandle color;
    FrameGraphTextureHandle depth;
    FrameGraphBufferHandle virtualShadowPageTable;
    VirtualShadowSamplingSnapshot virtualShadowSampling;
};
```

Do not rename `RendererData`. Keep its existing single-view fields only through Task 2 so current renderer call sites
continue compiling. Mark them as migration-only in a comment; Task 3 removes them after every consumer moves to indexed
views.

- [ ] **Step 4: Add explicit family and view pass contexts**

Keep `RenderPass` for family/output nodes and add a view-specific interface:

```cpp
class RenderPass : public NonCopyable
{
public:
    virtual ~RenderPass() = default;
    virtual void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) = 0;
};

class ViewRenderPass : public NonCopyable
{
public:
    virtual ~ViewRenderPass() = default;
    virtual void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) = 0;
};
```

Expose indexed view lookup from `RenderPassContext`:

```cpp
[[nodiscard]] const RendererViewData& GetView(UInt32 viewIndex) const
{
    VE_ASSERT(viewIndex < rendererData.views.size());
    return rendererData.views[viewIndex];
}
```

Keep `FrameGraphExecuteContext::rendererData` as one family-oriented `RendererData` reference.

- [ ] **Step 5: Run the focused test**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineRendererFrameGraphSmokeTests --output-on-failure
```

Expected: family data and indexed view tests pass.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h Engine/Runtime/Render/Renderer/RenderPass/RenderPass.cpp Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp Tests/Render/RendererFrameGraphSmokeTests.cpp
git commit -m "render: define family-oriented renderer data"
```

## Task 3: Move One Family And One FrameGraph Into BaseRenderer

**Files:**

- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/MobileRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/MobileRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RendererFactory.h`
- Modify: `Engine/Runtime/Render/Renderer/RendererFactory.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.h`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.cpp`
- Modify: `Engine/Runtime/Scene/SceneSystem.cpp`
- Modify: `Editor/RenderPass/SceneGridRenderPass.h`
- Modify: `Editor/RenderPass/SceneGridRenderPass.cpp`
- Modify: `Editor/RenderPass/EditorGizmoRenderPass.h`
- Modify: `Editor/RenderPass/EditorGizmoRenderPass.cpp`
- Modify: `Tests/Render/RendererFrameGraphSmokeTests.cpp`

- [ ] **Step 1: Add failing renderer ownership tests**

Add compile-time contract checks:

```cpp
template<typename T>
concept HasSingleViewField = requires(T value) { value.view; };

static_assert(std::is_same_v<decltype(ve::BaseRendererInitParam::viewFamily), ve::RenderViewFamily>);
static_assert(!HasSingleViewField<ve::BaseRendererInitParam>);
```

Add a pure queue helper test for one opaque queue and per-view transparent sorting:

```cpp
bool TestFamilyQueueShape()
{
    ve::RendererData data;
    data.views.resize(2);
    ve::BuildRendererQueues(data);
    return data.views.size() == 2 && data.opaqueItems.empty() &&
           data.views[0].transparentItems.empty() && data.views[1].transparentItems.empty();
}
```

Expose `BuildRendererQueues(RendererData&)` as a focused renderer helper rather than testing a full RHI renderer.
Append `TestFamilyQueueShape()` to the smoke executable's `main` success accumulator.

- [ ] **Step 2: Run the focused build and verify it fails**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
```

Expected: compilation fails on `viewFamily` and the family queue helper.

- [ ] **Step 3: Change BaseRenderer ownership and entry point**

Use:

```cpp
struct BaseRendererInitParam
{
    const FrameRenderPipelineData* frameData = nullptr;
    RenderViewFamily viewFamily;
    std::vector<std::unique_ptr<RenderPass>> outputPasses;
};

struct RendererViewPassExtension
{
    UInt32 viewIndex = 0;
    std::vector<std::unique_ptr<ViewRenderPass>> passes;
};

struct StandaloneRendererInitParam : public BaseRendererInitParam
{
    bool visualizeVirtualShadowPages = false;
    std::vector<RendererViewPassExtension> viewExtensions;
};

class BaseRenderer : public NonCopyable
{
public:
    virtual ~BaseRenderer() = default;
    [[nodiscard]] ErrorCode RenderScene();

protected:
    virtual void BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) = 0;
    [[nodiscard]] const RendererData& GetRendererData() const noexcept;
    [[nodiscard]] const FrameRenderPipelineData& GetFrameData() const noexcept;
};
```

The constructor moves `viewFamily.scene` into `rendererData_.scene` and creates one `RendererViewData` per family view.
`RenderScene()` builds queues, creates one `FrameGraph`, imports every view target, calls the virtual topology builder
once, registers output passes, exports observable outputs, then compiles and executes. Keep its transitional
`ErrorCode` return until Task 9 converts renderer internals to fail-fast void APIs after all graph work has migrated.

Compile/execute handling during this migration task remains:

```cpp
const Error compileResult = frameGraph.Compile();
if (!compileResult.IsOk())
{
    VE_LOG_ERROR("Family FrameGraph compile failed: %s", compileResult.GetMessage().c_str());
    return compileResult.GetCode();
}
return frameGraph.Execute();
```

- [ ] **Step 4: Import every view target**

Replace the single `target_` member with indexed imports:

```cpp
graphData.views.resize(rendererData_.views.size());
for (UInt32 viewIndex = 0; viewIndex < rendererData_.views.size(); ++viewIndex)
{
    RendererViewFrameGraphData& viewGraph = graphData.views[viewIndex];
    ImportViewRenderTargets(setupGraph, viewIndex, viewGraph);
}
```

Implement:

```cpp
void ImportViewRenderTargets(FrameGraph& graph,
                             UInt32 viewIndex,
                             RendererViewFrameGraphData& viewGraph) const;
```

The helper selects `rendererData_.views[viewIndex].view.target`, imports its live color/depth textures, or creates the
required transient depth matching the color extent. A null offscreen color texture is an invalid state. Only a view
explicitly targeting the main output imports the swapchain backing. The loop never creates a FrameGraph.

- [ ] **Step 5: Build shared and per-view queues**

Implement `BuildRendererQueues(RendererData&)` so it:

- scans `RTScene` once;
- classifies opaque items once into `RendererData::opaqueItems`;
- copies transparent candidates into every active view;
- stable-sorts each view's transparent list by that view camera position;
- treats missing material/unsupported queue as a fatal invariant;
- treats null scene or camera as an expected empty branch.

- [ ] **Step 6: Migrate Opaque and Transparent to ViewRenderPass**

Change both passes to:

```cpp
void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) override;
void Draw(const FrameGraphPassResources& resources,
          const PassData& passData,
          RenderPassContext& context);
```

Each pass captures `viewIndex`, reads `context.GetView(viewIndex)`, and accesses `graphData.views[viewIndex]`. Copy the
already prepared `RenderView::virtualShadowResult` into the indexed sampling snapshot so the existing external
`PreRenderShadowStep` remains functional until Task 8 moves VSM recording into this graph.

Update `StandaloneRenderer::BuildFrameGraph` and `MobileRenderer::BuildFrameGraph` to loop over
`GetRendererData().views` and add Opaque/Transparent once per view. Keep the `StandaloneRenderer` name.

Define `StandaloneRenderer::AddSceneAndExtensionPasses(FrameGraph&, RendererFrameGraphData&)` as the single helper that
registers each view's Opaque and Transparent passes, then registers every `RendererViewPassExtension` after validating
its `viewIndex`. `BuildFrameGraph` calls this helper in this checkpoint; later tasks insert DepthPrePass and VSM before
the helper without duplicating the scene/extension loop.

- [ ] **Step 7: Aggregate one family in the existing frame pipelines**

Keep the current `sceneRenderers` input temporarily, but replace the per-view renderer loop with:

```cpp
RenderViewFamily family;
family.scene = sceneRenderers_.empty() ? nullptr : sceneRenderers_.front().scene;
for (StandaloneRendererInitParam& source : sceneRenderers_)
{
    VE_ASSERT_ALWAYS_MESSAGE(source.scene == family.scene, "Editor renderer inputs must share one scene.");
    family.views.push_back(std::move(source.view));
}
const ErrorCode shadowResult = PrepareVirtualShadowFamily(frameData, family);
if (shadowResult != ErrorCode::None)
{
    return shadowResult;
}

StandaloneRendererInitParam familyRenderer;
familyRenderer.frameData = &frameData;
familyRenderer.viewFamily = std::move(family);
```

Move each input's Grid/Gizmo passes into a `RendererViewPassExtension` with the matching view index. Migrate
`SceneGridRenderPass` and `EditorGizmoRenderPass` to `ViewRenderPass`. Keep overlay and Player copy in their existing
separate graph/direct path until Task 5, so this checkpoint stays behaviorally equivalent while scene rendering already
uses one family graph.

In `SceneSystem.cpp`, replace the Player initialization assignments with:

```cpp
rendererInitParam.viewFamily.scene = impl.scene != nullptr ? impl.scene->GetRTScene() : nullptr;
RenderView playerView;
CameraComponent* camera = impl.scene != nullptr ? impl.scene->GetCamera() : nullptr;
playerView.camera = camera != nullptr ? camera->GetRTCamera() : nullptr;
VE_ASSERT_MESSAGE(impl.playerViewState != nullptr, "Player rendering requires a persistent render view state.");
playerView.viewState = impl.playerViewState->GetRTRenderViewState();
playerView.target.colorTexture = impl.playerSceneColorTexture;
rendererInitParam.viewFamily.views.push_back(std::move(playerView));
```

- [ ] **Step 8: Build the focused test and Editor**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: family tests and all migrated renderer/pass call sites compile.

- [ ] **Step 9: Commit**

```text
git add Engine/Runtime/Render/Renderer/BaseRenderer.h Engine/Runtime/Render/Renderer/BaseRenderer.cpp Engine/Runtime/Render/Renderer/StandaloneRenderer.h Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp Engine/Runtime/Render/Renderer/MobileRenderer.h Engine/Runtime/Render/Renderer/MobileRenderer.cpp Engine/Runtime/Render/Renderer/RendererFactory.h Engine/Runtime/Render/Renderer/RendererFactory.cpp Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp Engine/Runtime/Render/RenderFramePipeline.h Engine/Runtime/Render/RenderFramePipeline.cpp Engine/Runtime/Scene/SceneSystem.cpp Editor/RenderPass/SceneGridRenderPass.h Editor/RenderPass/SceneGridRenderPass.cpp Editor/RenderPass/EditorGizmoRenderPass.h Editor/RenderPass/EditorGizmoRenderPass.cpp Tests/Render/RendererFrameGraphSmokeTests.cpp
git commit -m "render: make renderer own one view family graph"
```

## Task 4: Add Generic DepthPrePass

**Files:**

- Create: `Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.h`
- Create: `Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `Tests/Render/RendererFrameGraphSmokeTests.cpp`

- [ ] **Step 1: Add a failing depth dependency test**

Add a test-only graph setup that records:

```text
DepthPrePass writes view depth version 1
VSM marker reads depth version 1
Opaque reads depth version 1 as a read-only/load dependency, then writes depth version 2
```

Assert the returned handles:

```cpp
passed &= Expect(depthAfterPrePass.IsValid(), "DepthPrePass should produce depth");
passed &= Expect(markInput == depthAfterPrePass, "VSM marking should consume pre-pass depth");
passed &= Expect(opaqueInput == depthAfterPrePass, "Opaque should load pre-pass depth");
```

Return this dependency check through the smoke executable's `main` success accumulator.

- [ ] **Step 2: Run the focused build and verify the pass is missing**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
```

Expected: compilation fails because `DepthPrePass` is missing.

- [ ] **Step 3: Implement the generic view pass**

Define:

```cpp
/// Writes opaque receiver depth for one family view.
///
/// Resource contract:
/// - reads RendererData::opaqueItems and the indexed view camera;
/// - writes only RendererFrameGraphData::views[viewIndex].depth;
/// - produces the depth version consumed by VSM request marking and Opaque.
class DepthPrePass final : public ViewRenderPass
{
public:
    void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData, UInt32 viewIndex) override;
};
```

Extract the receiver-depth pipeline construction/draw logic into the generic pass. Keep the legacy helper callable until
Task 8 switches VSM request generation to the graph-owned depth. The pass writes no color, uses `Clear` for depth, and
records draw calls with `void + assert`.

- [ ] **Step 4: Register and reuse depth**

`StandaloneRenderer` registers `DepthPrePass` for each renderable view before registering scene passes. Opaque declares
the existing depth version as a read-only/load input before producing its next depth version; it must not clear a valid
pre-pass result.

Keep `MobileRenderer` topology-specific: register `DepthPrePass` only when
`GetFrameData().device->GetBackend() != rhi::RhiBackend::Metal`, matching the current VSM capability boundary. The Metal
branch skips both generic VSM depth and VSM graph registration.

At this checkpoint the old external VSM preparation still records its receiver depth before Renderer execution, so the
depth draw occurs twice. This short-lived duplication keeps production output correct while the new generic dependency
is tested; Task 8 removes the legacy draw and leaves only `DepthPrePass`.

- [ ] **Step 5: Run focused tests and the Editor build**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineRendererFrameGraphSmokeTests --output-on-failure
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: depth dependency tests and Editor build pass.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.h Engine/Runtime/Render/Renderer/RenderPass/DepthPrePass.cpp Engine/Runtime/Render/Renderer/StandaloneRenderer.h Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp CMake/Targets/Engine.cmake Tests/Render/RendererFrameGraphSmokeTests.cpp
git commit -m "render: add family view depth prepass"
```

## Task 5: Move Product Outputs Into The Renderer Graph

**Files:**

- Create: `Engine/Runtime/Render/Renderer/RenderPass/SwapchainOutputRenderPass.h`
- Create: `Engine/Runtime/Render/Renderer/RenderPass/SwapchainOutputRenderPass.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.h`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.cpp`
- Modify: `Editor/Core/Editor.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `Tests/Render/RendererFrameGraphSmokeTests.cpp`

- [ ] **Step 1: Add failing output-pass contract tests**

Add:

```cpp
static_assert(std::is_same_v<decltype(ve::RendererViewPassExtension::passes),
                             std::vector<std::unique_ptr<ve::ViewRenderPass>>>);
static_assert(std::is_same_v<decltype(ve::BaseRendererInitParam::outputPasses),
                             std::vector<std::unique_ptr<ve::RenderPass>>>);
```

Add a model test that two Editor views produce one family and SceneView extensions reference index `0`, while GameView
has no extensions. Append it to the smoke executable's `main` success accumulator.

- [ ] **Step 2: Run the focused build and verify it fails**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
```

Expected: the output pass types and final one-family pipeline input are missing.

- [ ] **Step 3: Add output pass implementations**

Define:

```cpp
struct SwapchainOverlayRenderPassInitParam
{
    std::vector<UInt32> sampledViewIndices;
    rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
    rhi::RhiColor clearColor = {};
    SwapchainOverlayRenderCallback callback;
};

class SwapchainOverlayRenderPass final : public RenderPass
{
public:
    explicit SwapchainOverlayRenderPass(SwapchainOverlayRenderPassInitParam initParam);
    void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;
};

class ViewColorToSwapchainCopyPass final : public RenderPass
{
public:
    explicit ViewColorToSwapchainCopyPass(UInt32 viewIndex) noexcept;
    void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) override;
};
```

Declare `SwapchainOverlayRenderCallback` next to these renderer passes as
`std::function<void(rhi::RhiCommandList&)>`. Make `EditorOverlayRenderCallback` an alias of that renderer-layer type so
the renderer module does not depend on `RenderFramePipeline.h`.

Overlay setup reads every listed view color and writes swapchain color as a raster attachment. Copy setup reads the
selected view color with `ReadCopySource` and writes swapchain color with `WriteCopyDestination`; execute calls
`CopyTextureToSwapchain` and always-on asserts success.

- [ ] **Step 4: Aggregate one Editor family**

Replace `EditorRenderFramePipelineInitParam::sceneRenderers` with one frame-local `StandaloneRendererInitParam`. Update
`Editor.cpp` to:

- assign the shared active `RTScene` once;
- append SceneView and GameView to `viewFamily.views`;
- attach `SceneGridRenderPass` and `EditorGizmoRenderPass` to SceneView index `0`;
- append `SwapchainOverlayRenderPass` to base `outputPasses`;
- retain the referenced render textures through the existing frame lifetime list.

Grid/Gizmo already use the indexed `ViewRenderPass` contract from Task 3.

- [ ] **Step 5: Route Editor and Player output through the renderer graph**

Keep the transitional `ErrorCode` signature through Task 8, but remove the separate overlay graph and direct Player
copy:

```cpp
ErrorCode EditorRenderFramePipeline::RenderFrame(const FrameRenderPipelineData& frameData)
{
    VE_ASSERT_RENDER_THREAD();
    rhi::RhiCommandList& commandList = frameData.GetCommandList();
    if (!commandList.Begin())
    {
        return ErrorCode::PlatformError;
    }

    const ErrorCode shadowResult =
        PrepareVirtualShadowFamily(frameData, rendererInitParam_.viewFamily);
    if (shadowResult != ErrorCode::None)
    {
        const bool ended = commandList.End();
        VE_ASSERT_MESSAGE(ended, "Editor command list end after VSM failure failed.");
        return shadowResult;
    }

    rendererInitParam_.frameData = &frameData;
    StandaloneRenderer renderer(std::move(rendererInitParam_));
    const ErrorCode renderResult = renderer.RenderScene();
    if (renderResult != ErrorCode::None)
    {
        const bool ended = commandList.End();
        VE_ASSERT_MESSAGE(ended, "Editor command list end after renderer failure failed.");
        return renderResult;
    }
    return commandList.End() ? ErrorCode::None : ErrorCode::PlatformError;
}
```

Player follows the same shape, creates a one-view family, and appends `ViewColorToSwapchainCopyPass` to
`outputPasses`. Keep scene-color resize/resource replacement in the pipeline because it is an environment output
preparation step.

Remove `RecordSwapchainRasterPass`, the separate overlay FrameGraph, direct `CopySceneColorToSwapchain`, and renderer
loops. `PrepareVirtualShadowFamily` remains as the only temporary scheduling call and is deleted in Task 8.

- [ ] **Step 6: Build focused tests and Editor**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineRendererFrameGraphSmokeTests --output-on-failure
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor VEngineWinPlayer
```

Expected: one-family pipeline contracts pass and both applications build.

- [ ] **Step 7: Commit**

```text
git add Engine/Runtime/Render/Renderer/RenderPass/SwapchainOutputRenderPass.h Engine/Runtime/Render/Renderer/RenderPass/SwapchainOutputRenderPass.cpp Engine/Runtime/Render/RenderFramePipeline.h Engine/Runtime/Render/RenderFramePipeline.cpp Editor/Core/Editor.cpp CMake/Targets/Engine.cmake Tests/Render/RendererFrameGraphSmokeTests.cpp
git commit -m "render: move product output into renderer graph"
```

## Task 6: Prepare VSM Family Data And Import Persistent Resources

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `Tests/Render/RendererFrameGraphSmokeTests.cpp`

- [ ] **Step 1: Add failing prepared-family and sharing tests**

Require:

```cpp
struct VirtualShadowPreparedViewGraphData
{
    UInt32 viewIndex = 0;
    VirtualShadowFramePacket packet;
    VirtualShadowPageTableSlice slice;
    FrameGraphTextureHandle receiverDepth;
};

struct VirtualShadowPreparedFamilyGraphData
{
    VirtualShadowSceneCache* sceneCache = nullptr;
    std::vector<VirtualShadowPreparedViewGraphData> views;
    FrameGraphTextureHandle atlas;
    FrameGraphBufferHandle physicalPages;
    FrameGraphBufferHandle statistics;
};
```

Add a manager test with two views in one family that asserts:

- one scene identity;
- one `VirtualShadowSceneCache`;
- one atlas/physical/statistics backing;
- two distinct page-table slices;
- no GPU command is recorded while CPU preparation/import occurs.

- [ ] **Step 2: Run focused tests and verify the API is missing**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineRendererFrameGraphSmokeTests
```

Expected: compilation fails on the prepared graph-data contract and manager graph entry point.

- [ ] **Step 3: Add the manager graph entry point**

Expose:

```cpp
void AddToFrameGraph(const FrameRenderPipelineData& frameData,
                     FrameGraph& frameGraph,
                     RendererData& rendererData,
                     RendererFrameGraphData& graphData);
```

This method performs CPU-only work before registering nodes:

- obtain/create the scene cache;
- initialize the statistics slot as unavailable;
- scan directional light and caster invalidations once;
- register/validate every view and page-table slice;
- prepare `VirtualShadowFramePacket` values;
- publish complete enabled or placeholder sampling snapshots into indexed `RendererViewData`;
- start the prepared-frame transaction and fill the existing ticket;
- import atlas, page table, marks, request lists/counts, physical pages, statistics, and the current readback slot once.

Expected disabled states return normally and publish placeholders. Missing mandatory graph/frame services use always-on
assertions.

- [ ] **Step 4: Define internal VSM graph resource state**

In `VirtualShadowFrameGraph.h`, define the complete imported-handle bundle used only by the VSM registrar:

```cpp
struct VirtualShadowFrameGraphResources
{
    FrameGraphTextureHandle atlas;
    FrameGraphBufferHandle pageMarks;
    FrameGraphBufferHandle pageTable;
    FrameGraphBufferHandle requestList;
    FrameGraphBufferHandle requestCounts;
    FrameGraphBufferHandle physicalPages;
    FrameGraphBufferHandle statistics;
    FrameGraphBufferHandle statisticsReadback;
};
```

Slices remain offsets in prepared view data; FrameGraph tracks whole imported buffers in this stage, intentionally
serializing writes to shared resources.

- [ ] **Step 5: Keep the new registrar out of the production path until all VSM passes exist**

Exercise `AddToFrameGraph` directly from focused tests with its own frame index and transaction ticket. Do not call it
from `BaseRenderer` or `FrameRenderPipeline` in this checkpoint: the old `PreRenderShadowStep` still owns production VSM
recording, and registering both for the same frame would begin the cache transaction twice. Task 8 performs the single
production cutover after every required graph pass exists.

- [ ] **Step 6: Run focused tests**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineRendererFrameGraphSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R \"VEngine(VirtualShadow|RendererFrameGraph)Tests\" --output-on-failure
```

Expected: two-view sharing and CPU-only preparation tests pass.

- [ ] **Step 7: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.h Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowTypes.h CMake/Targets/Engine.cmake Tests/Unit/VirtualShadowTests.cpp Tests/Render/RendererFrameGraphSmokeTests.cpp
git commit -m "render: prepare VSM family graph data"
```

## Task 7: Add VSM Request And Residency Graph Passes

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.cpp`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp`

- [ ] **Step 1: Add failing pass-contract tests**

Add recording expectations for every active view:

```text
ClearScene:
  RW physicalPages, RW statistics

ClearViewRequests:
  RW pageMarks, RW pageTable, RW requestCounts, RW physicalPages

MarkRequests:
  R receiverDepth, RW pageMarks

CompactRequests:
  R pageMarks, RW requestList, RW requestCounts, RW statistics

ResolveHits(view, level):
  R requestList, R requestCounts, RW pageTable, RW physicalPages, RW statistics

AllocatePages(view, level):
  R pageMarks, RW pageTable, RW physicalPages, RW statistics
```

In the real-backend smoke trace, interleave an unrelated side-effect pass between registration groups and assert the
observed execute trace still places each consumer after the producer of its declared resource version.

- [ ] **Step 2: Run unit/smoke builds and verify the focused passes are missing**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineVirtualShadowGpuPipelineSmokeTests
```

Expected: compilation fails on request/residency registrar symbols.

- [ ] **Step 3: Implement and document request passes**

Declare focused registration functions whose comments state scope and dependencies:

```cpp
void AddVirtualShadowClearScenePass(FrameGraph& graph,
                                    VirtualShadowPreparedFamilyGraphData& family,
                                    VirtualShadowFrameGraphResources& resources);
void AddVirtualShadowClearViewRequestsPass(FrameGraph& graph,
                                           const VirtualShadowPreparedViewGraphData& view,
                                           VirtualShadowFrameGraphResources& resources);
void AddVirtualShadowMarkRequestsPass(FrameGraph& graph,
                                      const VirtualShadowPreparedViewGraphData& view,
                                      VirtualShadowFrameGraphResources& resources);
void AddVirtualShadowCompactRequestsPass(FrameGraph& graph,
                                         const VirtualShadowPreparedViewGraphData& view,
                                         VirtualShadowFrameGraphResources& resources);
```

Extract the corresponding HLSL, pipeline-layout construction, uniform upload, and dispatch recording into focused
`void` recording helpers used by the graph execute callbacks. Required pipelines, buffers, and uniform allocations are
always-on asserted. Until Task 8 cuts production over, keep `VirtualShadowGpuPipeline::ClearSceneFrame` and
`PrepareViewRequests` as thin compatibility adapters that invoke the same helpers and return `ErrorCode::None`; do not
run both schedulers in one frame.

- [ ] **Step 4: Implement and document residency passes**

Define:

```cpp
void AddVirtualShadowResolveHitsPass(FrameGraph& graph,
                                     const VirtualShadowPreparedViewGraphData& view,
                                     UInt32 level,
                                     VirtualShadowFrameGraphResources& resources);
void AddVirtualShadowAllocatePagesPass(FrameGraph& graph,
                                       const VirtualShadowPreparedViewGraphData& view,
                                       UInt32 level,
                                       VirtualShadowFrameGraphResources& resources);
```

Register resolve nodes coarse-to-fine across the family, then register allocation nodes in deterministic
family-view/coarse-to-fine priority. For every read-write access, replace the stored handle with the returned output
version:

```cpp
passData.physicalPages = builder.Write(resources.physicalPages);
resources.physicalPages = passData.physicalPages;
```

This creates the dependency chain that pins earlier resident/allocated pages before later allocation. Keep
`ResolveViewHits` and `AllocateViewPages` as matching temporary compatibility adapters until Task 8.

- [ ] **Step 5: Run focused unit and real-backend smoke tests**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineVirtualShadowGpuPipelineSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R \"VEngineVirtualShadow(Tests|GpuPipelineSmokeTests)\" --output-on-failure
```

Expected: pass contracts and D3D11/D3D12 request/allocation execution pass.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.h Engine/Runtime/Render/VirtualShadow/VirtualShadowRequestPasses.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.h Engine/Runtime/Render/VirtualShadow/VirtualShadowResidencyPasses.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp CMake/Targets/Engine.cmake Tests/Unit/VirtualShadowTests.cpp Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp
git commit -m "render: schedule VSM requests through frame graph"
```

## Task 8: Complete And Cut Over The VSM FrameGraph Path

**Files:**

- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.h`
- Create: `Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/MobileRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/MobileRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.h`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`
- Modify: `Tests/Render/RendererFrameGraphSmokeTests.cpp`
- Modify: `Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp`

- [ ] **Step 1: Add failing final-stage dependency tests**

Require:

```text
ClearPhysicalPages(view):
  R physicalPages, W atlas depth

RenderCasters(view):
  R physicalPages, load+W atlas depth

MarkRendered(view):
  RW physicalPages, RW statistics

FinalizeScene:
  RW physicalPages, RW statistics

StatisticsReadback:
  R(CopySource) statistics, W(CopyDestination) statisticsReadback

Opaque(view):
  R finalized atlas, R finalized page-table slice, load pre-pass depth
```

Add smoke assertions that statistics readback executes after finalization and before command-list end without `WaitIdle`.

- [ ] **Step 2: Run focused builds and verify the pass APIs are missing**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineVirtualShadowTests VEngineVirtualShadowGpuPipelineSmokeTests
```

Expected: compilation fails on page/finalization pass symbols.

- [ ] **Step 3: Split physical page raster into two graph passes**

Define and document:

```cpp
void AddVirtualShadowClearPhysicalPagesPass(FrameGraph& graph,
                                            const VirtualShadowPreparedViewGraphData& view,
                                            VirtualShadowFrameGraphResources& resources);
void AddVirtualShadowRenderCastersPass(FrameGraph& graph,
                                       const VirtualShadowPreparedViewGraphData& view,
                                       VirtualShadowFrameGraphResources& resources);
```

The clear pass writes the atlas with `Load` and rasterizes only selected physical page rectangles. The caster pass loads
the clear pass output, renders the view's shadow casters, and publishes the next atlas version. Both read the current
physical metadata version. Factor their recording bodies so the legacy `RenderViewPages` adapter can invoke them during
the first half of this task; the adapter is no longer called after Step 6 and is deleted in Task 9.

- [ ] **Step 4: Add rendered/finalize/readback passes**

Define:

```cpp
void AddVirtualShadowMarkRenderedPass(FrameGraph& graph,
                                      const VirtualShadowPreparedViewGraphData& view,
                                      VirtualShadowFrameGraphResources& resources);
void AddVirtualShadowFinalizeScenePass(FrameGraph& graph,
                                       VirtualShadowPreparedFamilyGraphData& family,
                                       VirtualShadowFrameGraphResources& resources);
void AddVirtualShadowStatisticsReadbackPass(FrameGraph& graph,
                                            VirtualShadowFrameGraphResources& resources);
```

Readback setup uses:

```cpp
passData.source = builder.ReadCopySource(resources.statistics);
passData.destination = builder.WriteCopyDestination(resources.statisticsReadback);
resources.statisticsReadback = passData.destination;
graph.Export(resources.statisticsReadback);
```

Execute records the exact `sizeof(VirtualShadowGpuStatistics)` buffer copy and never maps or waits.

Keep `FinalizeSceneFrame` and `CopySceneStatistics` as thin adapters over the same focused recording helpers until the
production cutover in Step 6. This preserves a buildable old path while the final graph nodes and their tests are being
added.

- [ ] **Step 5: Publish final sampling handles to scene passes**

After final VSM registration:

- store the final atlas handle in `RendererFrameGraphData::virtualShadowAtlas`;
- store each view's final page-table handle/slice and immutable snapshot in its view graph data;
- make Opaque and Transparent read those handles;
- preserve placeholder bindings for disabled VSM without importing invalid resources.

This produces the graph edges `final RenderCasters atlas version -> Opaque -> Transparent` and
`final page-table version -> Opaque -> Transparent`. The separate physical-metadata chain orders
`RenderCasters -> MarkRendered -> FinalizeScene -> StatisticsReadback`.

- [ ] **Step 6: Cut production scheduling over exactly once**

Register VSM from the renderer topology after the generic depth producers and before the scene consumers:

```cpp
void StandaloneRenderer::BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData)
{
    for (UInt32 viewIndex = 0; viewIndex < GetRendererData().views.size(); ++viewIndex)
    {
        depthPrePass_.AddToFrameGraph(frameGraph, graphData, viewIndex);
    }

    VirtualShadowManager* virtualShadowManager = GetFrameData().virtualShadowManager;
    VE_ASSERT_ALWAYS_MESSAGE(virtualShadowManager != nullptr, "Standalone renderer requires the RenderSystem VSM manager.");
    virtualShadowManager->AddToFrameGraph(
        GetFrameData(), frameGraph, GetMutableRendererData(), graphData);

    AddSceneAndExtensionPasses(frameGraph, graphData);
}
```

Add the protected `GetMutableRendererData()` accessor solely for registrars that publish immutable per-view sampling
snapshots during graph construction. `MobileRenderer` also calls the manager entry point. On Metal,
`VirtualShadowManager::AddToFrameGraph` takes its expected disabled branch before requiring receiver depth, publishes
placeholder sampling into every indexed view, starts no prepared transaction, and registers no VSM passes.

Remove `PrepareVirtualShadowFamily` and the legacy receiver-depth recording call from both Editor and Player frame
pipelines. They still own command-list begin/end and retain their transitional `ErrorCode RenderFrame` signatures until
Task 9. The renderer graph is now the only code allowed to begin the prepared VSM transaction or record VSM GPU work.

Add a focused regression test with two views that counts:

- one prepared scene transaction;
- one generic `DepthPrePass` per active Standalone view;
- one VSM family registrar invocation;
- no legacy `PreRenderShadowStep` invocation;
- Opaque sampling only after the finalized VSM handles exist.

Append the regression to the renderer smoke executable's `main` success accumulator.

- [ ] **Step 7: Run focused tests and smoke**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests VEngineVirtualShadowTests VEngineVirtualShadowGpuPipelineSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R \"VEngine(RendererFrameGraph|VirtualShadow)\" --output-on-failure
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor VEngineWinPlayer
```

Expected: D3D11/D3D12 atlas rendering, fence-delayed statistics readback, one-family Editor/Player integration, and the
single-scheduler regression all pass.

- [ ] **Step 8: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.h Engine/Runtime/Render/VirtualShadow/VirtualShadowPagePasses.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowFrameGraph.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.cpp Engine/Runtime/Render/Renderer/BaseRenderer.h Engine/Runtime/Render/Renderer/BaseRenderer.cpp Engine/Runtime/Render/Renderer/StandaloneRenderer.h Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp Engine/Runtime/Render/Renderer/MobileRenderer.h Engine/Runtime/Render/Renderer/MobileRenderer.cpp Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp Engine/Runtime/Render/RenderFramePipeline.h Engine/Runtime/Render/RenderFramePipeline.cpp CMake/Targets/Engine.cmake Tests/Render/RendererFrameGraphSmokeTests.cpp Tests/Unit/VirtualShadowTests.cpp Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp
git commit -m "render: cut VSM recording over to frame graph"
```

## Task 9: Remove Legacy VSM Scheduling And Renderer Error Propagation

**Files:**

- Delete: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.h`
- Delete: `Engine/Runtime/Render/VirtualShadow/VirtualShadowGpuPipeline.cpp`
- Rename: `Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp` to `Tests/Render/VirtualShadowFrameGraphSmokeTests.cpp`
- Rename: `CMake/Targets/Tests/VirtualShadowGpuPipelineSmokeTests.cmake` to `CMake/Targets/Tests/VirtualShadowFrameGraphSmokeTests.cmake`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowManager.cpp`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.h`
- Modify: `Engine/Runtime/Render/VirtualShadow/VirtualShadowRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h`
- Modify: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.h`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.cpp`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `CMake/Targets/Tests.cmake`
- Modify: `Tests/Render/RendererFrameGraphSmokeTests.cpp`
- Modify: `Tests/Unit/VirtualShadowTests.cpp`

- [ ] **Step 1: Add a source/API regression test**

Add compile-time checks for void internal APIs and remove all test references to direct legacy stages. Add a CMake/script
check in the test source contract:

```cpp
static_assert(std::is_void_v<decltype(std::declval<ve::FrameRenderPipeline&>().RenderFrame(
    std::declval<const ve::FrameRenderPipelineData&>()))>);
static_assert(std::is_void_v<decltype(std::declval<ve::BaseRenderer&>().Render())>);
```

Run:

```text
rg -n \"PreRenderShadowStep|PrepareViewRequests|ResolveViewHits|AllocateViewPages|RenderViewPages|FinalizeSceneFrame\" Engine Editor Tests
```

Expected before removal: matches remain in the legacy pipeline and tests.

- [ ] **Step 2: Remove PreRenderShadowStep and direct pipeline methods**

Delete:

- `VirtualShadowManager::PreRenderShadowStep`;
- `VirtualShadowGpuPipeline`;
- all direct stage calls and error-recovery branches that disabled one view after a stage returned `ErrorCode`.

Rename the real-backend smoke source, CMake include, executable target, and CTest name from
`VirtualShadowGpuPipelineSmokeTests` to `VirtualShadowFrameGraphSmokeTests`; update its assertions to exercise only the
manager/renderer graph registrar.

Normal disabled cases remain prepared as placeholders. Missing required pipelines, resources, uniforms, or command
recording capability now log stage/view/level context and use `VE_ASSERT_ALWAYS_MESSAGE`.

- [ ] **Step 3: Finish void conversion in scene and Editor passes**

Rename the renderer entry point and change the frame-pipeline contract:

```cpp
class BaseRenderer : public NonCopyable
{
public:
    void Render();
};

class FrameRenderPipeline : public NonCopyable
{
public:
    virtual void RenderFrame(const FrameRenderPipelineData& frameData) = 0;
};
```

`BaseRenderer::Render()` still calls `FrameGraph::Compile()` and `FrameGraph::Execute()`, because validation remains a
recoverable boundary, but converts a failure into a contextual log followed by `VE_ASSERT_ALWAYS_MESSAGE`. Editor and
Player `RenderFrame()` assert command-list begin/end failures rather than returning them. Update `RenderSystem` to call
the void pipeline entry point and continue handling queue submission, fence, swapchain, Present, and device errors at
their existing external boundaries.

Change private methods such as:

```cpp
void Draw(...);
void EnsurePipeline(...);
void BindVirtualShadowSampling(...);
```

Assert required pipeline/resource/uniform state at the point of use. Remove `return ErrorCode::None`, pass-result
branches, `[[nodiscard]]` markers on void methods, and `ToString(ErrorCode)` logging that existed only for internal
propagation.

Do not change RenderSystem/RHI initialization, resize, submit, fence, Present, or platform recovery signatures.

- [ ] **Step 4: Verify no legacy scheduling symbols remain**

Run:

```text
rg -n \"PreRenderShadowStep|PrepareViewRequests|ResolveViewHits|AllocateViewPages|RenderViewPages|FinalizeSceneFrame|VirtualShadowGpuPipeline\" Engine Editor Tests CMake
```

Expected: no matches.

Run:

```text
rg -n \"ErrorCode (RenderFrame|RenderScene|Render|Draw|AddToFrameGraph)\" Engine/Runtime/Render Editor/RenderPass
```

Expected: no renderer-internal propagation signatures; retained FrameGraph/RHI/RenderSystem boundaries are not matched.

- [ ] **Step 5: Build and run focused tests**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests --target VEngineRendererFrameGraphSmokeTests VEngineVirtualShadowTests VEngineVirtualShadowFrameGraphSmokeTests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R \"VEngine(RendererFrameGraph|VirtualShadow)\" --output-on-failure
```

Expected: all focused tests pass using only the graph path.

- [ ] **Step 6: Commit**

```text
git add Engine/Runtime/Render/VirtualShadow Engine/Runtime/Render/Renderer Engine/Runtime/Render/RenderFramePipeline.h Engine/Runtime/Render/RenderFramePipeline.cpp Engine/Runtime/Render/RenderSystem.cpp CMake/Targets/Engine.cmake CMake/Targets/Tests.cmake CMake/Targets/Tests/VirtualShadowGpuPipelineSmokeTests.cmake CMake/Targets/Tests/VirtualShadowFrameGraphSmokeTests.cmake Tests/Render/RendererFrameGraphSmokeTests.cpp Tests/Unit/VirtualShadowTests.cpp Tests/Render/VirtualShadowGpuPipelineSmokeTests.cpp Tests/Render/VirtualShadowFrameGraphSmokeTests.cpp
git commit -m "render: remove legacy VSM stage scheduling"
```

## Task 10: Document And Verify The Complete Migration

**Files:**

- Modify: `Docs/ArchitectureOverview.md`
- Modify: `Docs/RenderSystemDesign.md`

- [ ] **Step 1: Update architecture documentation**

Document:

- FrameRenderPipeline as the frame-local Editor/Player environment layer;
- one frame-local renderer per family;
- one FrameGraph owned inside that renderer;
- retained `StandaloneRenderer` and `RendererData` names;
- generic DepthPrePass ownership and consumers;
- VSM pass resource dependencies and RenderSystem-owned persistent cache boundary;
- output pass semantics;
- void/assert internal failure policy and retained recoverable boundaries.

Remove descriptions of `PreRenderShadowStep` and per-view renderer invocation.

- [ ] **Step 2: Review scope and formatting**

Run:

```text
git diff --check
git status --short
rg -n \"PreRenderShadowStep|one single-view BaseRenderer|per-view renderer\" Docs Engine Editor Tests
```

Expected:

- no whitespace errors;
- only intended files are changed;
- no stale architecture descriptions or production symbols remain.

- [ ] **Step 3: Run the full Windows test build and suite**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-tests
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: all tests, including the new renderer/frame-graph target and D3D11/D3D12 VSM smoke, pass.

- [ ] **Step 4: Build all Debug applications and tools**

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: `VEngineWinEditor`, `VEngineWinPlayer`, shader/asset tools, and enabled RHI backends build.

- [ ] **Step 5: Run the real VSM smoke executable directly**

```text
Build/windows-msvc-tests/Debug/VEngineVirtualShadowFrameGraphSmokeTests.exe
```

Expected: D3D11 and D3D12 family graph execution, allocation, atlas rendering, and statistics readback pass with exit code
zero.

- [ ] **Step 6: Run deterministic Editor startup smoke**

Launch:

```text
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Verify:

- one Editor family contains SceneView and GameView;
- Grid/Gizmo appear only in SceneView;
- both views render through one FrameGraph;
- VSM statistics continue updating;
- no project-selection automation is used.

- [ ] **Step 7: Request code review**

Use the `requesting-code-review` skill. Review specifically for:

- no renderer or pass object owns cross-frame cache state;
- one graph covers the full family and product output;
- every VSM ordering constraint is a resource-version dependency;
- depth pre-pass is generic and reused;
- allocation priority is preserved;
- statistics read occurs only after the matching FrameContext fence;
- fail-fast conversion did not remove recoverable RenderSystem/RHI boundaries;
- no old scheduling path remains.

Address all critical and important findings, then rerun the affected focused tests.

- [ ] **Step 8: Commit documentation**

```text
git add Docs/ArchitectureOverview.md Docs/RenderSystemDesign.md
git commit -m "docs: describe family frame graph rendering"
```

- [ ] **Step 9: Perform final verification**

Use the `verification-before-completion` skill and record exact passing commands, test counts, direct smoke exit code,
Editor startup result, final commit IDs, and any intentionally retained user changes.
