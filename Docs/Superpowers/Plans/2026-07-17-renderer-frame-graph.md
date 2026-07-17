# Renderer Frame Graph Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move renderer-owned code under `Engine/Runtime/Render/Renderer`, replace `ForwardRenderer` with platform-specific renderers, execute scene passes through a compiled frame graph, and route opaque/transparent items through material render queues.

**Architecture:** `BaseRenderer` builds per-view render queues and owns one build/compile/execute graph invocation. `StandaloneRenderer` and `MobileRenderer` register their own pass topology; typed versioned texture handles describe dependencies, while each in-flight `FrameContext` owns a fence-safe transient texture pool. Material JSON supplies the render queue, and scene passes consume only the queue list assigned to them.

**Tech Stack:** C++20, CMake, VEngine common RHI, Boost.JSON, MSVC/CTest through `CMake/Scripts/WithMsvc.bat`.

---

## File map

New renderer-owned files:

- `Engine/Runtime/Render/Renderer/RenderQueue.h/.cpp`: queue enum plus strict JSON string conversion.
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h/.cpp`: typed handles, texture descriptions, access declarations, and resolved resources.
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphTransientResourcePool.h/.cpp`: descriptor-keyed physical texture reuse.
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h/.cpp`: pass-scoped read/write and raster attachment declarations.
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h/.cpp`: pass type erasure, compile, cull, stable topological sort, lifetime calculation, and execution.
- `Engine/Runtime/Render/Renderer/BaseRenderer.h/.cpp`: common render target, render queue construction, graph orchestration, and error propagation.
- `Engine/Runtime/Render/Renderer/StandaloneRenderer.h/.cpp`: host renderer and editor-pass injection.
- `Engine/Runtime/Render/Renderer/MobileRenderer.h/.cpp`: mobile topology without editor-pass injection.
- `Engine/Runtime/Render/Renderer/RendererFactory.h/.cpp`: compile-time player renderer selection.
- `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h/.cpp`: graph-facing pass interface and draw context.
- `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h/.cpp`: opaque-list pass registration and drawing.
- `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h/.cpp`: transparent-list pass with alpha blending and read-only depth.

Moved/replaced files and integration points:

- Delete `Engine/Runtime/Render/BaseRenderer.h/.cpp` and `Engine/Runtime/Render/RenderPass/*` after their replacements compile.
- Modify `Engine/Runtime/Render/FrameContext.h/.cpp` and `RenderFramePipelineData.h`: own and expose the transient pool.
- Modify `Engine/Runtime/Render/RenderResource.h`, `Engine/Runtime/Resource/ResourceObject.h/.cpp`: carry strict material render-queue data.
- Modify `Engine/Runtime/Render/RenderFramePipeline.h/.cpp`, `Engine/Runtime/Scene/SceneSystem.cpp`, `Editor/Core/Editor.cpp`, and editor pass files: instantiate concrete renderers and register graph passes.
- Modify `Engine/Runtime/Render/RenderSystem.h/.cpp` and `CMake/Targets/Engine.cmake`: update includes and explicit source lists.

## Task 1: Introduce the material render-queue contract

**Files:**

- Create: `Engine/Runtime/Render/Renderer/RenderQueue.h`
- Create: `Engine/Runtime/Render/Renderer/RenderQueue.cpp`
- Modify: `Engine/Runtime/Render/RenderResource.h`
- Modify: `Engine/Runtime/Resource/ResourceObject.h`
- Modify: `Engine/Runtime/Resource/ResourceObject.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Run the absent-contract check**

Run:

```powershell
if (Test-Path Engine/Runtime/Render/Renderer/RenderQueue.h) { throw 'RenderQueue unexpectedly exists' }
```

Expected: command succeeds because the queue contract does not yet exist.

- [ ] **Step 2: Define numeric queue anchors and strict conversion**

Create this public contract:

```cpp
enum class RenderQueue : UInt16
{
    Opaque = 2000,
    Transparent = 3000,
};

[[nodiscard]] const char* ToString(RenderQueue renderQueue) noexcept;
[[nodiscard]] Result<RenderQueue> ParseRenderQueue(std::string_view value);
```

`ParseRenderQueue` returns `Opaque` or `Transparent` for exact case-sensitive matches and returns `ErrorCode::InvalidArgument` with the unknown value in the message for every other string.

- [ ] **Step 3: Parse and retain the material setting**

Add `RenderQueue renderQueue_ = RenderQueue::Opaque;` to `MaterialResource`, parse the optional JSON field before resolving properties, and copy the value into `RTMaterialResourceDesc`:

```cpp
const boost::json::value* renderQueueValue = materialJson.GetValue().if_contains("renderQueue");
renderQueue_ = RenderQueue::Opaque;
if (renderQueueValue != nullptr)
{
    if (!renderQueueValue->is_string())
    {
        return Error(ErrorCode::InvalidArgument, "Material renderQueue must be a string: " + GetRuntimePath().GetString());
    }

    Result<RenderQueue> parsedQueue = ParseRenderQueue(renderQueueValue->as_string().c_str());
    if (!parsedQueue)
    {
        return Error(parsedQueue.GetError().GetCode(), parsedQueue.GetError().GetMessage() + ": " + GetRuntimePath().GetString());
    }
    renderQueue_ = parsedQueue.GetValue();
}
```

Extend the render-thread descriptor exactly as follows and assign it in `BuildRenderDesc()`:

```cpp
struct RTMaterialResourceDesc
{
    std::string name = "MaterialResource";
    std::vector<std::byte> constantData;
    std::shared_ptr<RTShaderResource> shaderResource;
    RenderQueue renderQueue = RenderQueue::Opaque;
    UInt64 revision = 0;
};
```

- [ ] **Step 4: Register the files and build**

Replace the old renderer entries in the CMake source list only as each new file is introduced; for this task add `RenderQueue.cpp` to `PRIVATE` and `RenderQueue.h` to `PUBLIC`.

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: build succeeds and existing materials without `renderQueue` remain opaque.

- [ ] **Step 5: Commit the queue data path**

```text
git add Engine/Runtime/Render/Renderer/RenderQueue.h Engine/Runtime/Render/Renderer/RenderQueue.cpp Engine/Runtime/Render/RenderResource.h Engine/Runtime/Resource/ResourceObject.h Engine/Runtime/Resource/ResourceObject.cpp CMake/Targets/Engine.cmake
git commit -m "render: add material render queues"
```

## Task 2: Add typed frame-graph texture resources and a per-context pool

**Files:**

- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.h`
- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphResource.cpp`
- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphTransientResourcePool.h`
- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphTransientResourcePool.cpp`
- Modify: `Engine/Runtime/Render/FrameContext.h`
- Modify: `Engine/Runtime/Render/FrameContext.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipelineData.h`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Run the absent-resource check**

Run:

```powershell
rg -n "FrameGraphResource|FrameGraphTransientResourcePool" Engine/Runtime/Render
```

Expected: no matches and `rg` exits with code 1.

- [ ] **Step 2: Define handles, texture descriptors, and access categories**

Use an invalid sentinel so default-constructed handles are rejected:

```cpp
inline constexpr UInt32 InvalidFrameGraphResourceIndex = std::numeric_limits<UInt32>::max();

template<typename T>
struct FrameGraphResource
{
    UInt32 index = InvalidFrameGraphResourceIndex;
    UInt32 version = 0;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return index != InvalidFrameGraphResourceIndex;
    }
};

struct FrameGraphTexture
{
};

using FrameGraphTextureHandle = FrameGraphResource<FrameGraphTexture>;

enum class FrameGraphTextureAccess
{
    ColorAttachment,
    DepthAttachment,
    ShaderRead,
};

struct FrameGraphTextureDesc
{
    rhi::RhiTextureDimension dimension = rhi::RhiTextureDimension::Texture2D;
    UInt32 width = 0;
    UInt32 height = 0;
    UInt32 depth = 1;
    UInt32 mipLevelCount = 1;
    rhi::RhiFormat format = rhi::RhiFormat::Rgba8Unorm;
    rhi::RhiTextureUsage usage = rhi::RhiTextureUsage::Sampled;
};

struct ImportedFrameGraphTexture
{
    rhi::RhiTexture* texture = nullptr;
    bool isSwapchain = false;
};
```

Implement equality and hashing over every physical descriptor field; resource names and native pointers do not participate in the pool key.

- [ ] **Step 3: Implement descriptor-keyed physical reuse**

Expose this pool API:

```cpp
class FrameGraphTransientResourcePool final : public NonCopyable
{
public:
    void Initialize(rhi::RhiDevice& device) noexcept;
    [[nodiscard]] std::unique_ptr<rhi::RhiTexture> AcquireTexture(const FrameGraphTextureDesc& desc, const char* debugName);
    void ReleaseTexture(const FrameGraphTextureDesc& desc, std::unique_ptr<rhi::RhiTexture> texture);
    void Shutdown() noexcept;

private:
    rhi::RhiDevice* device_ = nullptr;
    std::unordered_map<FrameGraphTextureDesc,
                       std::vector<std::unique_ptr<rhi::RhiTexture>>,
                       FrameGraphTextureDescHash>
        availableTextures_;
};
```

`AcquireTexture` pops a matching texture or converts the graph descriptor to `rhi::RhiTextureDesc` and calls `CreateTexture`. `ReleaseTexture` pushes into the exact descriptor bucket. `Shutdown` clears all buckets and nulls the device.

- [ ] **Step 4: Attach the pool to fence-safe frame contexts**

Add `FrameGraphTransientResourcePool transientResourcePool_;` to `FrameContext`; initialize it after command-list/fence creation, preserve it in `WaitAndReset`, and shut it down after the final wait. Add forwarding accessors:

```cpp
[[nodiscard]] FrameGraphTransientResourcePool& GetFrameGraphTransientResourcePool() noexcept;
```

to both `FrameContext` and `FrameRenderPipelineData`. Do not clear the pool from `WaitAndReset`; the completed fence makes its available textures safe for reuse.

- [ ] **Step 5: Register and build**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: build succeeds; existing two-frame context rotation still owns synchronization.

- [ ] **Step 6: Commit resource primitives**

```text
git add Engine/Runtime/Render/Renderer/FrameGraph Engine/Runtime/Render/FrameContext.h Engine/Runtime/Render/FrameContext.cpp Engine/Runtime/Render/RenderFramePipelineData.h CMake/Targets/Engine.cmake
git commit -m "render: add frame graph texture resources"
```

## Task 3: Implement frame-graph build, compile, culling, and execution

**Files:**

- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.h`
- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphBuilder.cpp`
- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`
- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Establish a failing graph contract check**

Run:

```powershell
rg -n "Compile\(|Export\(|AddRasterPass" Engine/Runtime/Render/Renderer/FrameGraph
```

Expected: no complete graph API is found.

- [ ] **Step 2: Define the graph-facing pass API**

Expose the following operations while keeping node storage private to `FrameGraph`:

```cpp
class FrameGraph final : public NonCopyable
{
public:
    explicit FrameGraph(FrameGraphExecuteContext context) noexcept;

    template<typename PassData, typename SetupCallback, typename ExecuteCallback>
    void AddRasterPass(std::string name, SetupCallback&& setupCallback, ExecuteCallback&& executeCallback);

    [[nodiscard]] FrameGraphTextureHandle CreateTexture(std::string name, FrameGraphTextureDesc desc);
    [[nodiscard]] FrameGraphTextureHandle
    ImportTexture(std::string name, FrameGraphTextureDesc desc, ImportedFrameGraphTexture importedTexture);
    void Export(FrameGraphTextureHandle handle);
    [[nodiscard]] Error Compile();
    [[nodiscard]] ErrorCode Execute();
};

struct FrameGraphExecuteContext
{
    const FrameRenderPipelineData& frameData;
    const RendererData& rendererData;
};
```

`AddRasterPass` allocates `std::shared_ptr<PassData>`, invokes setup immediately through `FrameGraphBuilder`, and type-erases execution into `std::function<ErrorCode(const FrameGraphPassContext&)>`. This keeps pass data alive until execution without exposing raw node storage.

- [ ] **Step 3: Implement exact-version builder declarations**

Expose these pass-scoped methods:

```cpp
[[nodiscard]] FrameGraphTextureHandle Read(FrameGraphTextureHandle handle, FrameGraphTextureAccess access);
[[nodiscard]] FrameGraphTextureHandle Write(FrameGraphTextureHandle handle, FrameGraphTextureAccess access);
void SetColorAttachment(FrameGraphTextureHandle handle,
                        rhi::RhiLoadAction loadAction,
                        rhi::RhiStoreAction storeAction,
                        rhi::RhiColor clearColor);
void SetDepthAttachment(FrameGraphTextureHandle handle,
                        rhi::RhiLoadAction loadAction,
                        rhi::RhiStoreAction storeAction,
                        rhi::RhiDepthStencilClearValue clearValue,
                        bool readOnly);
void SetRenderArea(const rhi::RhiRenderArea& renderArea) noexcept;
void SetViewport(const rhi::RhiViewport& viewport) noexcept;
void SetScissor(const rhi::RhiScissorRect& scissorRect) noexcept;
void SetSideEffect() noexcept;
```

`Write` validates the incoming version at compile time, records consumption of the old version, appends a new version whose producer is the current pass, and returns it. Attachment setters require a handle already declared with the corresponding access; they do not create hidden dependencies.

- [ ] **Step 4: Compile deterministically**

Implement `Compile()` in this order:

1. Reject invalid indices, unavailable versions, access/attachment mismatches, missing imported native resources, zero-sized transient descriptions, and multiple producers for one version.
2. Add producer-to-reader edges for reads, old-producer-to-writer edges for version writes, reader-to-later-writer edges for write-after-read, and previous-writer-to-next-writer edges for write-after-write.
3. Root side-effect passes and producers contributing to every exported handle; recursively retain their dependencies.
4. Run Kahn topological sort over retained nodes, selecting the lowest registration index whenever multiple nodes are ready.
5. Return `ErrorCode::InvalidState` with pass names when retained nodes remain after sorting.
6. Calculate first producer and final retained user for every transient base resource.

Keep the compiled order immutable until execute completes; any build mutation clears the prior compiled result.

- [ ] **Step 5: Execute compiled raster passes**

For each retained pass in compiled order:

1. Acquire transient textures whose first retained producer is the pass.
2. Resolve every attachment version to either the imported pointer, swapchain sentinel, or acquired texture.
3. Build `rhi::RhiRenderPassDesc`; use a null color attachment only when `isSwapchain` is true.
4. Call `BeginRenderPass`, set viewport/scissor, invoke the type-erased callback, and always call `EndRenderPass` after a successful begin.
5. Abort remaining graph execution on the first non-`None` result.
6. Release transient textures after their final retained user, including the failure path for every texture already acquired.

`Execute()` returns `InvalidState` when called before a successful compile and `PlatformError` when the RHI cannot begin a pass.

- [ ] **Step 6: Register, build, and inspect the contract**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
rg -n "Write\(|Export\(|compiledPassOrder_|AcquireTexture|ReleaseTexture" Engine/Runtime/Render/Renderer/FrameGraph
```

Expected: build succeeds and each source-contract term appears in the graph implementation.

- [ ] **Step 7: Commit the graph core**

```text
git add Engine/Runtime/Render/Renderer/FrameGraph CMake/Targets/Engine.cmake
git commit -m "render: implement compiled frame graph"
```

## Task 4: Move the pass interface and register queue-specific scene passes

**Files:**

- Create: `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h`
- Create: `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.cpp`
- Create: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.h`
- Create: `Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp`
- Create: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.h`
- Create: `Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp`
- Delete later in Task 8: `Engine/Runtime/Render/RenderPass/RenderPass.h/.cpp`
- Delete later in Task 8: `Engine/Runtime/Render/RenderPass/OpaqueSceneRenderPass.h/.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Demonstrate the old pass contract**

Run:

```powershell
rg -n "virtual void Setup\(RenderPassBuilder|scene->GetRenderItem" Engine/Runtime/Render/RenderPass
```

Expected: the old immediate setup interface and whole-scene traversal are both found.

- [ ] **Step 2: Define renderer data and graph pass registration**

Move renderer data into the new pass header and make queue ownership explicit:

```cpp
struct RendererData
{
    std::shared_ptr<RTScene> scene;
    std::shared_ptr<RTCamera> resolvedCamera;
    std::vector<std::shared_ptr<RTRenderItem>> opaqueItems;
    std::vector<std::shared_ptr<RTRenderItem>> transparentItems;
};

struct RendererFrameGraphData
{
    FrameGraphTextureHandle color;
    FrameGraphTextureHandle depth;
};

class RenderPass : public NonCopyable
{
public:
    virtual ~RenderPass() = default;
    virtual void AddToFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) = 0;
};
```

Keep `RenderPassContext` as the draw-time facade over `FrameRenderPipelineData`, `RendererData`, resolved pass state, device, and command list.

- [ ] **Step 3: Convert opaque rendering to a graph pass**

`OpaqueSceneRenderPass::AddToFrameGraph` must declare the first color/depth writes and update the blackboard handles:

```cpp
struct OpaquePassData
{
    FrameGraphTextureHandle color;
    FrameGraphTextureHandle depth;
};

frameGraph.AddRasterPass<OpaquePassData>(
    "OpaqueScenePass",
    [&](FrameGraphBuilder& builder, OpaquePassData& passData)
    {
        passData.color = builder.Write(graphData.color, FrameGraphTextureAccess::ColorAttachment);
        builder.SetColorAttachment(passData.color, colorLoadAction_, colorStoreAction_, clearColor_);
        graphData.color = passData.color;
        if (graphData.depth.IsValid())
        {
            passData.depth = builder.Write(graphData.depth, FrameGraphTextureAccess::DepthAttachment);
            builder.SetDepthAttachment(passData.depth,
                                       rhi::RhiLoadAction::Clear,
                                       rhi::RhiStoreAction::Store,
                                       rhi::RhiDepthStencilClearValue{},
                                       false);
            graphData.depth = passData.depth;
        }
    },
    [this](const OpaquePassData&, RenderPassContext& context)
    {
        return Draw(context, context.rendererData.opaqueItems);
    });
```

Preserve the existing uniform slots and mesh/material validation, but select the shader from the first usable opaque item and iterate only `opaqueItems`. Return `ErrorCode::None` for an empty list.

- [ ] **Step 4: Add transparent rendering with the correct state**

Register the pass after opaque. Its color attachment loads and writes the current color version; its depth attachment reads the current depth version and is marked read-only:

```cpp
passData.color = builder.Write(graphData.color, FrameGraphTextureAccess::ColorAttachment);
builder.SetColorAttachment(passData.color, rhi::RhiLoadAction::Load, rhi::RhiStoreAction::Store, rhi::RhiColor{});
graphData.color = passData.color;

if (graphData.depth.IsValid())
{
    passData.depth = builder.Read(graphData.depth, FrameGraphTextureAccess::DepthAttachment);
    builder.SetDepthAttachment(passData.depth,
                               rhi::RhiLoadAction::Load,
                               rhi::RhiStoreAction::Store,
                               rhi::RhiDepthStencilClearValue{},
                               true);
}
```

The transparent pipeline uses:

```cpp
pipelineDesc.blendState = rhi::StaticRenderStates::AlphaBlend;
pipelineDesc.depthStencilState = depthEnabled ? rhi::StaticRenderStates::DepthReadOnlyLessEqual : rhi::StaticRenderStates::DepthDisabled;
```

It iterates only `transparentItems` and retains the existing frame/view/object/material uniform bindings.

- [ ] **Step 5: Build the new passes**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: new passes and old renderer still compile together temporarily; no new pass loops over `RTScene`.

- [ ] **Step 6: Commit graph-facing passes**

```text
git add Engine/Runtime/Render/Renderer/RenderPass CMake/Targets/Engine.cmake
git commit -m "render: register opaque and transparent graph passes"
```

## Task 5: Replace ForwardRenderer with base, standalone, mobile, and factory types

**Files:**

- Create: `Engine/Runtime/Render/Renderer/BaseRenderer.h`
- Create: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Create: `Engine/Runtime/Render/Renderer/StandaloneRenderer.h`
- Create: `Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp`
- Create: `Engine/Runtime/Render/Renderer/MobileRenderer.h`
- Create: `Engine/Runtime/Render/Renderer/MobileRenderer.cpp`
- Create: `Engine/Runtime/Render/Renderer/RendererFactory.h`
- Create: `Engine/Runtime/Render/Renderer/RendererFactory.cpp`
- Modify: `CMake/Targets/Engine.cmake`

- [ ] **Step 1: Record the failing renderer-selection contract**

Run:

```powershell
rg -n "StandaloneRenderer|MobileRenderer|CreatePlayerRenderer" Engine/Runtime/Render
```

Expected: no matches.

- [ ] **Step 2: Define common and concrete init data**

Use one common parameter shape and restrict pass injection to standalone:

```cpp
struct RendererRenderTarget
{
    std::shared_ptr<RTRenderTexture> colorTexture;
    rhi::RhiLoadAction colorLoadAction = rhi::RhiLoadAction::Clear;
    rhi::RhiStoreAction colorStoreAction = rhi::RhiStoreAction::Store;
};

struct BaseRendererInitParam
{
    const FrameRenderPipelineData* frameData = nullptr;
    std::shared_ptr<RTScene> scene;
    std::shared_ptr<RTCamera> camera;
    RendererRenderTarget target;
    rhi::RhiFillMode fillMode = rhi::RhiFillMode::Solid;
};

struct StandaloneRendererInitParam : BaseRendererInitParam
{
    std::vector<std::unique_ptr<RenderPass>> additionalPasses;
};

struct MobileRendererInitParam : BaseRendererInitParam
{
};
```

- [ ] **Step 3: Build queues once per renderer invocation**

`BaseRenderer::BuildRenderQueues()` scans the scene once. It asserts and logs missing material proxies, appends by `material->GetDesc().renderQueue`, and sorts transparent items stably by descending squared distance:

```cpp
const Matrix44& cameraTransform = rendererData_.resolvedCamera->GetLocalToWorld();
const Vector3 cameraPosition(cameraTransform.Get(0, 3), cameraTransform.Get(1, 3), cameraTransform.Get(2, 3));

std::stable_sort(rendererData_.transparentItems.begin(),
                 rendererData_.transparentItems.end(),
                 [&cameraPosition](const auto& left, const auto& right)
                 {
                     const Vector3 leftCenter = left->GetLocalToWorld().TransformPoint(left->GetBoundsCenter());
                     const Vector3 rightCenter = right->GetLocalToWorld().TransformPoint(right->GetBoundsCenter());
                     return (leftCenter - cameraPosition).LengthSquared() > (rightCenter - cameraPosition).LengthSquared();
                 });
```

If the camera is null, return `ErrorCode::None` without registering scene passes. Opaque order remains the stable scene order.

- [ ] **Step 4: Orchestrate graph import, compile, and execute**

Change `RenderScene()` to return `ErrorCode`. Build color/depth descriptors from either `RTRenderTexture` or the swapchain, import them, call the concrete topology hook, export the final color version, compile, log compilation diagnostics, and execute:

```cpp
virtual void BuildFrameGraph(FrameGraph& frameGraph, RendererFrameGraphData& graphData) = 0;

[[nodiscard]] ErrorCode BaseRenderer::RenderScene()
{
    BuildRenderQueues();
    FrameGraph frameGraph(FrameGraphExecuteContext{*frameRenderData_, rendererData_});
    RendererFrameGraphData graphData = ImportRenderTargets(frameGraph);
    BuildFrameGraph(frameGraph, graphData);
    frameGraph.Export(graphData.color);
    Error compileResult = frameGraph.Compile();
    if (!compileResult.IsOk())
    {
        VE_LOG_ERROR("Frame graph compile failed: %s", compileResult.GetMessage().c_str());
        return compileResult.GetCode();
    }
    return frameGraph.Execute();
}
```

- [ ] **Step 5: Register standalone and mobile topologies**

Both renderers own opaque and transparent pass instances. `StandaloneRenderer::BuildFrameGraph` adds opaque, transparent, then each `additionalPasses` entry. `MobileRenderer::BuildFrameGraph` adds opaque and transparent only. Neither implementation branches on platform at pass-registration time.

- [ ] **Step 6: Select the player implementation at compile time**

Expose:

```cpp
[[nodiscard]] std::unique_ptr<BaseRenderer> CreatePlayerRenderer(BaseRendererInitParam initParam);
```

Implement strict selection:

```cpp
#if VE_PLATFORM_IOS
    return std::make_unique<MobileRenderer>(MobileRendererInitParam{std::move(initParam)});
#elif VE_PLATFORM_WINDOWS || VE_PLATFORM_MACOS
    return std::make_unique<StandaloneRenderer>(StandaloneRendererInitParam{std::move(initParam), {}});
#else
#error Unsupported player renderer platform.
#endif
```

Use explicit constructors for the derived init parameters if aggregate base initialization is rejected by MSVC.

- [ ] **Step 7: Register, build, and commit**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: all four renderer types compile before call sites are switched.

```text
git add Engine/Runtime/Render/Renderer/BaseRenderer.h Engine/Runtime/Render/Renderer/BaseRenderer.cpp Engine/Runtime/Render/Renderer/StandaloneRenderer.h Engine/Runtime/Render/Renderer/StandaloneRenderer.cpp Engine/Runtime/Render/Renderer/MobileRenderer.h Engine/Runtime/Render/Renderer/MobileRenderer.cpp Engine/Runtime/Render/Renderer/RendererFactory.h Engine/Runtime/Render/Renderer/RendererFactory.cpp CMake/Targets/Engine.cmake
git commit -m "render: add standalone and mobile renderers"
```

## Task 6: Convert editor passes to graph registration

**Files:**

- Modify: `Editor/RenderPass/SceneGridRenderPass.h`
- Modify: `Editor/RenderPass/SceneGridRenderPass.cpp`
- Modify: `Editor/RenderPass/EditorGizmoRenderPass.h`
- Modify: `Editor/RenderPass/EditorGizmoRenderPass.cpp`

- [ ] **Step 1: Confirm editor passes still use immediate setup**

Run:

```powershell
rg -n "void (SceneGridRenderPass|EditorGizmoRenderPass)::Setup" Editor/RenderPass
```

Expected: both old setup methods are found.

- [ ] **Step 2: Register the grid through the graph**

Replace `GetName/Setup/Execute` overrides with `AddToFrameGraph`. The setup callback writes and loads the current color handle, reads and loads depth when valid, updates `graphData.color`, and preserves the target-derived viewport. The execute callback calls the existing draw body through a private `ErrorCode Execute(RenderPassContext&)` helper and returns `None` after a successful or intentionally empty draw.

Use this attachment declaration:

```cpp
passData.color = builder.Write(graphData.color, FrameGraphTextureAccess::ColorAttachment);
builder.SetColorAttachment(passData.color, rhi::RhiLoadAction::Load, rhi::RhiStoreAction::Store, rhi::RhiColor{});
graphData.color = passData.color;
if (graphData.depth.IsValid())
{
    passData.depth = builder.Read(graphData.depth, FrameGraphTextureAccess::DepthAttachment);
    builder.SetDepthAttachment(passData.depth,
                               rhi::RhiLoadAction::Load,
                               rhi::RhiStoreAction::Store,
                               rhi::RhiDepthStencilClearValue{},
                               true);
}
```

- [ ] **Step 3: Register gizmos after the current color version**

Apply the same `AddToFrameGraph` pattern. Gizmos write/load color; declare depth only if the existing gizmo pipeline actually enables depth. Preserve icon-atlas texture/sampler binding and draw-list behavior. Update includes to `Engine/Runtime/Render/Renderer/RenderPass/RenderPass.h` and frame-graph headers.

- [ ] **Step 4: Build and prove editor pass ordering is declarative**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
rg -n "AddToFrameGraph|builder\.Write" Editor/RenderPass
```

Expected: build succeeds and both editor passes register graph writes.

- [ ] **Step 5: Commit editor graph passes**

```text
git add Editor/RenderPass/SceneGridRenderPass.h Editor/RenderPass/SceneGridRenderPass.cpp Editor/RenderPass/EditorGizmoRenderPass.h Editor/RenderPass/EditorGizmoRenderPass.cpp
git commit -m "editor: register viewport passes in frame graph"
```

## Task 7: Switch frame pipelines and products to concrete renderers

**Files:**

- Modify: `Engine/Runtime/Render/RenderFramePipeline.h`
- Modify: `Engine/Runtime/Render/RenderFramePipeline.cpp`
- Modify: `Engine/Runtime/Scene/SceneSystem.cpp`
- Modify: `Editor/Core/Editor.cpp`
- Modify: `Engine/Runtime/Render/RenderSystem.h`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`

- [ ] **Step 1: Capture the old call-site contract**

Run:

```powershell
rg -n "ForwardRenderer" Engine Editor Player
```

Expected: frame pipeline, scene system, editor, render system, and old renderer files contain matches.

- [ ] **Step 2: Make editor pipelines explicitly standalone**

Change editor init/storage types to `std::vector<StandaloneRendererInitParam>`. For each renderer, set `frameData`, construct `StandaloneRenderer`, call `RenderScene()`, and immediately propagate a non-`None` result after ending the command list. In `Editor.cpp`, create `StandaloneRendererInitParam` and append grid/gizmo objects to `additionalPasses`.

- [ ] **Step 3: Make player pipelines factory-selected**

Store `BaseRendererInitParam` in `PlayerRenderFramePipelineInitParam`. During rendering, set `frameData`, call `CreatePlayerRenderer(std::move(sceneRenderer_))`, validate the result, invoke `RenderScene()`, and propagate failures before copying the scene color to the swapchain.

Use this failure shape for both products:

```cpp
const ErrorCode renderResult = renderer.RenderScene();
if (renderResult != ErrorCode::None)
{
    const bool ended = commandList.End();
    VE_ASSERT_MESSAGE(ended, "Frame pipeline failed to end command list after renderer failure.");
    return renderResult;
}
```

- [ ] **Step 4: Build player init data without renderer aliases**

In `SceneSystem.cpp`, replace `ForwardRendererInitParam` with `BaseRendererInitParam`; keep the selected scene, first camera, and player render texture unchanged. Update render-system includes to the new renderer headers without adding aliases or compatibility shims.

- [ ] **Step 5: Build the complete product integration**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: `VEngine`, `VEnginePlayer`, and `VEngineEditor` link with the new renderer call sites.

- [ ] **Step 6: Commit product integration**

```text
git add Engine/Runtime/Render/RenderFramePipeline.h Engine/Runtime/Render/RenderFramePipeline.cpp Engine/Runtime/Scene/SceneSystem.cpp Editor/Core/Editor.cpp Engine/Runtime/Render/RenderSystem.h Engine/Runtime/Render/RenderSystem.cpp
git commit -m "render: select renderer by product platform"
```

## Task 8: Remove old paths and compatibility symbols

**Files:**

- Delete: `Engine/Runtime/Render/BaseRenderer.h`
- Delete: `Engine/Runtime/Render/BaseRenderer.cpp`
- Delete: `Engine/Runtime/Render/RenderPass/RenderPass.h`
- Delete: `Engine/Runtime/Render/RenderPass/RenderPass.cpp`
- Delete: `Engine/Runtime/Render/RenderPass/OpaqueSceneRenderPass.h`
- Delete: `Engine/Runtime/Render/RenderPass/OpaqueSceneRenderPass.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: every include reported by the source-contract scan

- [ ] **Step 1: Remove obsolete files and source-list entries**

Delete the six old files only after Tasks 1-7 build. Remove their `PRIVATE`/`PUBLIC` CMake entries and ensure every new renderer, graph, and pass file is explicitly listed in the matching section.

- [ ] **Step 2: Enforce the no-compatibility contract**

Run:

```powershell
$matches = rg -n "ForwardRenderer|Engine/Runtime/Render/BaseRenderer|Engine/Runtime/Render/RenderPass" Engine Editor Player CMake
if ($LASTEXITCODE -eq 0) { $matches; throw 'Obsolete renderer contract remains' }
if ($LASTEXITCODE -ne 1) { throw 'rg failed' }
```

Expected: no matches. Do not add aliases, forwarding headers, deprecated typedefs, or fallback include paths.

- [ ] **Step 3: Enforce pass queue isolation and platform selection**

Run:

```powershell
$sceneLoops = rg -n "GetRenderItemCount|GetRenderItem\(" Engine/Runtime/Render/Renderer/RenderPass
if ($LASTEXITCODE -eq 0) { $sceneLoops; throw 'A render pass still traverses RTScene' }
rg -n "opaqueItems" Engine/Runtime/Render/Renderer/RenderPass/OpaqueSceneRenderPass.cpp
rg -n "transparentItems|AlphaBlend|DepthReadOnlyLessEqual" Engine/Runtime/Render/Renderer/RenderPass/TransparentSceneRenderPass.cpp
rg -n "VE_PLATFORM_IOS|VE_PLATFORM_WINDOWS|VE_PLATFORM_MACOS" Engine/Runtime/Render/Renderer/RendererFactory.cpp
```

Expected: no scene traversal; queue-specific lists and all required platform macros are present.

- [ ] **Step 4: Build after the destructive cleanup**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Expected: clean configure and build succeed with no old file paths.

- [ ] **Step 5: Commit the hard removal**

```text
git add -A Engine/Runtime/Render Engine/Runtime/Scene Editor CMake/Targets/Engine.cmake
git commit -m "render: remove forward renderer compatibility"
```

## Task 9: Full verification and audit

**Files:**

- Inspect: all changed files

- [ ] **Step 1: Run formatting checks and format changed C++ files**

Use the repository `.clang-format` on all changed `.h/.cpp` files, then run:

```text
git diff --check
```

Expected: no whitespace errors.

- [ ] **Step 2: Run the complete Windows build and tests**

Run:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

Expected: configure succeeds, all targets build, and all registered tests pass.

- [ ] **Step 3: Run the final architecture contract**

Run:

```powershell
$old = rg -n "ForwardRenderer|Engine/Runtime/Render/BaseRenderer|Engine/Runtime/Render/RenderPass" Engine Editor Player CMake
if ($LASTEXITCODE -eq 0) { $old; throw 'Old renderer API remains' }
$passSceneLoops = rg -n "GetRenderItemCount|GetRenderItem\(" Engine/Runtime/Render/Renderer/RenderPass
if ($LASTEXITCODE -eq 0) { $passSceneLoops; throw 'Render pass bypasses queue lists' }
rg -n "Compile\(|compiledPassOrder_|Export\(" Engine/Runtime/Render/Renderer/FrameGraph
rg -n "StandaloneRenderer|MobileRenderer" Engine/Runtime/Render/Renderer/RendererFactory.cpp
rg -n "Opaque = 2000|Transparent = 3000" Engine/Runtime/Render/Renderer/RenderQueue.h
```

Expected: both negative scans are empty and every positive graph/renderer/queue scan finds its contract.

- [ ] **Step 4: Audit the complete worktree**

Run:

```text
git status --short
git diff --stat HEAD~8..HEAD
git log --oneline -10
```

Expected: only intentional renderer work is present; commits are ordered by dependency and no generated build products are staged.

- [ ] **Step 5: Commit verification-only corrections if formatting changed files**

```text
git add Engine/Runtime/Render Engine/Runtime/Resource/ResourceObject.h Engine/Runtime/Resource/ResourceObject.cpp Engine/Runtime/Scene/SceneSystem.cpp Editor CMake/Targets/Engine.cmake
git commit -m "style: format renderer frame graph changes"
```

Skip this commit when formatting produced no diff.
