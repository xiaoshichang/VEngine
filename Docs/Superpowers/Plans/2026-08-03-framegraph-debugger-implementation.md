# FrameGraph Debugger Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an Editor Frame Graph panel that pauses Play Mode and captures the next rendered frame's immutable graph metadata plus scaled previews of every initialized logical texture version.

**Architecture:** A one-shot request travels through `RenderSystem` to the next render frame. `FrameGraph` first compiles and snapshots the original graph, then injects hidden reader passes that preserve and convert initialized texture versions; after queue submission succeeds, the render thread publishes `std::shared_ptr<const FrameGraphDebugData>` for the panel to own. The panel renders pass nodes with `imgui-node-editor`, tables and details below, and explicitly returns replaced data to the render thread for fence-safe RHI retirement.

**Tech Stack:** C++20, CMake, CTest, Dear ImGui 1.92.8, imgui-node-editor, D3D11, D3D12, Metal, HLSL, MSL.

---

## File Map

New runtime files:

- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h`: immutable debug records, preview metadata, capture requests/status, and exchange API.
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp`: scale helpers, source-graph-to-snapshot conversion, capture planning, dependency hazard classification, and thread-safe exchange.
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h`: render-thread preview texture and conversion-pass interface.
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.cpp`: persistent preview allocation, HLSL/MSL conversion pipelines, fullscreen draw, staging-copy decisions, and render-thread reset.

New Editor files:

- `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.h`: panel state, node-editor context, selection, filters, and owned `shared_ptr`.
- `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.cpp`: toolbar, capture/pause action, node topology, lower tables, details, preview zoom/pan, and data replacement/retirement.
- `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h`: pure UI eligibility, stable node/pin/link ID encoding, and filter helpers.
- `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.cpp`: testable panel-model implementation without ImGui dependencies.

New dependency files:

- `CMake/ThirdParty/SetupImGuiNodeEditor.cmake`: builds and exposes `VEngine::ImGuiNodeEditor`.
- `ThirdParty/ImGuiNodeEditor/main.py`: validates the pinned vendored payload.
- `ThirdParty/ImGuiNodeEditor/Build_Windows64.bat`, `Build_Mac.sh`, `README.md`, `.gitignore`, `.gitattributes`: repository-standard entry points, revision documentation, and stable vendored line endings.
- `ThirdParty/ImGuiNodeEditor/imgui-node-editor-55a7dbf/`: pinned PR #335 head snapshot based on upstream master/WIP 0.9.4, including both ImGui 1.92.8 compatibility patches.

New tests:

- `Tests/Unit/FrameGraphDebugTests.cpp`: preview sizing, source snapshot, hazards, capture plan, one-shot exchange, shared result transfer, and panel-model behavior.
- `CMake/Targets/Tests/FrameGraphDebugTests.cmake`: independent CTest executable.

Modified integration files:

- `CMakeLists.txt`, `CMake/Targets/Engine.cmake`, `CMake/Targets/Applications/Common.cmake`, `CMake/Targets/Applications/Windows.cmake`, `CMake/Targets/Applications/Mac.cmake`, `CMake/Targets/Tests.cmake`.
- `ThirdParty/main.py`, `ThirdParty/README.md`.
- `Engine/RHI/Common/RhiDevice.h`, `Engine/RHI/D3D11/D3D11Rhi.cpp`, `Engine/RHI/D3D12/D3D12Rhi.cpp`, `Engine/RHI/Metal/MetalRhi.mm`.
- `Engine/Runtime/Render/RenderFramePipelineData.h`, `Engine/Runtime/Render/RenderSystem.h`, `Engine/Runtime/Render/RenderSystem.cpp`.
- `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`, `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`, `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`.
- `Editor/Core/Editor.cpp`, `Editor/Core/EditorProjectEditingView.h`, `Editor/Core/EditorProjectEditingView.cpp`.

## Task 1: Vendor And Link imgui-node-editor

**Files:**

- Create the dependency files listed above under `ThirdParty/ImGuiNodeEditor/`.
- Create: `CMake/ThirdParty/SetupImGuiNodeEditor.cmake`
- Modify: `CMakeLists.txt`
- Modify: `ThirdParty/main.py`
- Modify: `ThirdParty/README.md`
- Modify: `CMake/Targets/Applications/Windows.cmake`
- Modify: `CMake/Targets/Applications/Mac.cmake`

- [ ] **Step 1: Add a validation command and run it before the payload exists**

Add the dispatch branch to `ThirdParty/main.py`:

```python
if dependency == "imgui-node-editor":
    return run(root / "ImGuiNodeEditor" / "main.py", *args)
```

Run:

```powershell
python ThirdParty/main.py imgui-node-editor
```

Expected: non-zero exit identifying the missing pinned source files.

- [ ] **Step 2: Vendor the pinned MIT source and compatibility changes**

The latest official upstream tag is `v0.9.3`; there is no `v0.9.4` tag. Import the exact PR #335 head commit `55a7dbf`, based on upstream master/WIP 0.9.4, into `ThirdParty/ImGuiNodeEditor/imgui-node-editor-55a7dbf/`. Include `LICENSE`, `crude_json.*`, `imgui_bezier_math.*`, `imgui_canvas.*`, `imgui_extra_math.*`, `imgui_node_editor.*`, and `imgui_node_editor_internal.*`. The snapshot combines PR #335 / commit `186081d` for the `ImVec2` operator guard and PR #339 / commit `ca3d8d2` for the four ImGui 1.92.8 drawing call-site changes. `README.md` records the tag, pull request, and commit provenance.

Record the SHA-256 digest of all 13 tracked upstream files, including `LICENSE`, in the validator. It must report every missing file before hashing, then report each
content mismatch with its path and expected/actual digest. Pin the vendored directory to LF with `.gitattributes` so validation is stable across Windows and macOS
checkouts.

- [ ] **Step 3: Add the CMake wrapper**

`SetupImGuiNodeEditor.cmake` must build only the library sources and publicly link Dear ImGui:

```cmake
set(VE_IMGUI_NODE_EDITOR_REVISION "55a7dbf" CACHE STRING "imgui-node-editor revision")
set(VE_IMGUI_NODE_EDITOR_SOURCE_DIR "" CACHE PATH "Optional imgui-node-editor development source override")
set(_VE_IMGUI_NODE_EDITOR_DEFAULT_SOURCE_DIR
    "${PROJECT_SOURCE_DIR}/ThirdParty/ImGuiNodeEditor/imgui-node-editor-${VE_IMGUI_NODE_EDITOR_REVISION}")

if(VE_IMGUI_NODE_EDITOR_SOURCE_DIR)
    set(_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR "${VE_IMGUI_NODE_EDITOR_SOURCE_DIR}")
else()
    set(_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR "${_VE_IMGUI_NODE_EDITOR_DEFAULT_SOURCE_DIR}")
endif()

function(ve_add_imgui_node_editor_library)
    if(TARGET VEngineImGuiNodeEditor)
        return()
    endif()
    add_library(VEngineImGuiNodeEditor STATIC
        "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}/crude_json.cpp"
        "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}/imgui_canvas.cpp"
        "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}/imgui_node_editor.cpp"
    )
    add_library(VEngine::ImGuiNodeEditor ALIAS VEngineImGuiNodeEditor)
    target_include_directories(VEngineImGuiNodeEditor SYSTEM PUBLIC "${_VE_IMGUI_NODE_EDITOR_EFFECTIVE_SOURCE_DIR}")
    target_link_libraries(VEngineImGuiNodeEditor PUBLIC VEngine::ImGui)
    target_compile_features(VEngineImGuiNodeEditor PUBLIC cxx_std_20)
    set_target_properties(VEngineImGuiNodeEditor PROPERTIES CXX_EXTENSIONS OFF)
endfunction()

function(ve_setup_imgui_node_editor target_name)
    ve_add_imgui_node_editor_library()
    target_link_libraries(${target_name} PRIVATE VEngine::ImGuiNodeEditor)
endfunction()
```

The wrapper validates required filenames for every source tree and enforces all pinned SHA-256 digests when using the derived repository default. An explicitly supplied
`VE_IMGUI_NODE_EDITOR_SOURCE_DIR` is a development override and is not tied to upstream hashes. Migrate the original cached derived source path to the empty override
without treating it as user intent. Include the wrapper after `SetupImGui.cmake` and call `ve_setup_imgui_node_editor` for both Editor targets after `ve_setup_imgui`.

- [ ] **Step 4: Validate and configure**

Run:

```powershell
python ThirdParty/main.py imgui-node-editor
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
```

Expected: validator exits zero and CMake reports both Dear ImGui and imgui-node-editor targets without compatibility compile errors.

- [ ] **Step 5: Commit**

```powershell
git add CMakeLists.txt CMake/ThirdParty/SetupImGuiNodeEditor.cmake CMake/Targets/Applications/Windows.cmake CMake/Targets/Applications/Mac.cmake ThirdParty/ImGuiNodeEditor ThirdParty/main.py ThirdParty/README.md
git commit -m "build: add imgui node editor dependency"
```

## Task 2: Define Debug Records And Preview Math

**Files:**

- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h`
- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp`
- Create: `Tests/Unit/FrameGraphDebugTests.cpp`
- Create: `CMake/Targets/Tests/FrameGraphDebugTests.cmake`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `CMake/Targets/Tests.cmake`

- [ ] **Step 1: Register the empty test target**

```cmake
function(ve_add_frame_graph_debug_tests)
    add_executable(VEngineFrameGraphDebugTests Tests/Unit/FrameGraphDebugTests.cpp)
    target_link_libraries(VEngineFrameGraphDebugTests PRIVATE VEngine)
    ve_configure_target(VEngineFrameGraphDebugTests)
    add_test(NAME VEngineFrameGraphDebugTests COMMAND $<TARGET_FILE:VEngineFrameGraphDebugTests>)
endfunction()
```

Include it from `CMake/Targets/Tests.cmake` and invoke it from `ve_add_tests()`.

- [ ] **Step 2: Write failing extent and scale tests**

```cpp
bool TestPreviewExtent()
{
    bool passed = true;
    passed &= Expect(ve::IsFrameGraphDebugPreviewScaleValid(0.1F), "0.1 scale should be valid");
    passed &= Expect(ve::IsFrameGraphDebugPreviewScaleValid(1.0F), "1.0 scale should be valid");
    passed &= Expect(!ve::IsFrameGraphDebugPreviewScaleValid(0.09F), "scale below 0.1 should be rejected");
    passed &= Expect(!ve::IsFrameGraphDebugPreviewScaleValid(1.01F), "scale above 1.0 should be rejected");
    passed &= Expect(ve::CalculateFrameGraphDebugPreviewExtent(3, 5, 0.5F) == ve::rhi::RhiExtent2D{2, 3},
                     "preview dimensions should round and stay non-zero");
    passed &= Expect(ve::CalculateFrameGraphDebugPreviewExtent(1, 1, 0.1F) == ve::rhi::RhiExtent2D{1, 1},
                     "preview dimensions should clamp to one pixel");
    return passed;
}
```

- [ ] **Step 3: Run the test and confirm the missing API failure**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
```

Expected: compile failure naming `IsFrameGraphDebugPreviewScaleValid` and `CalculateFrameGraphDebugPreviewExtent`.

- [ ] **Step 4: Implement the immutable value model and helpers**

Define these stable categories and records in `FrameGraphDebug.h`:

```cpp
enum class FrameGraphDebugCaptureStatus { Idle, Armed, Capturing, Ready, Failed };
enum class FrameGraphDebugResourceKind { Texture, Buffer };
enum class FrameGraphDebugDependencyHazard { Raw, War, Waw };
enum class FrameGraphDebugPreviewState { Unavailable, Ready, Failed };
enum class FrameGraphDebugPassType { Raster, Compute };

struct FrameGraphDebugCaptureRequest
{
    UInt64 requestId = 0;
    Float32 previewScale = 0.5F;
};

struct FrameGraphDebugDependency
{
    UInt32 beforePass = InvalidFrameGraphResourceIndex;
    UInt32 afterPass = InvalidFrameGraphResourceIndex;
    FrameGraphDebugResourceKind resourceKind = FrameGraphDebugResourceKind::Texture;
    UInt32 resourceIndex = InvalidFrameGraphResourceIndex;
    UInt32 version = 0;
    FrameGraphDebugDependencyHazard hazard = FrameGraphDebugDependencyHazard::Raw;
};
```

Define the published record shape explicitly:

```cpp
inline constexpr UInt32 InvalidFrameGraphDebugPassIndex = std::numeric_limits<UInt32>::max();

struct FrameGraphDebugAccess
{
    FrameGraphDebugResourceKind resourceKind = FrameGraphDebugResourceKind::Texture;
    UInt32 resourceIndex = InvalidFrameGraphResourceIndex;
    UInt32 inputVersion = 0;
    std::optional<UInt32> outputVersion;
    UInt32 accessValue = 0;
    bool write = false;
};

struct FrameGraphDebugAttachment
{
    UInt32 textureIndex = InvalidFrameGraphResourceIndex;
    UInt32 version = 0;
    rhi::RhiLoadAction loadAction = rhi::RhiLoadAction::DontCare;
    rhi::RhiStoreAction storeAction = rhi::RhiStoreAction::DontCare;
    bool depth = false;
    bool readOnly = false;
};

struct FrameGraphDebugPass
{
    std::string name;
    FrameGraphDebugPassType type = FrameGraphDebugPassType::Raster;
    UInt32 registrationIndex = InvalidFrameGraphDebugPassIndex;
    std::optional<UInt32> compiledIndex;
    bool retained = false;
    bool culled = false;
    bool sideEffect = false;
    std::vector<FrameGraphDebugAccess> accesses;
    std::vector<FrameGraphDebugAttachment> attachments;
    std::vector<FrameGraphTextureHandle> textureUavBarriers;
    std::vector<FrameGraphBufferHandle> bufferUavBarriers;
};

struct FrameGraphDebugPreview
{
    FrameGraphDebugPreviewState state = FrameGraphDebugPreviewState::Unavailable;
    std::string message;
    rhi::RhiExtent2D sourceExtent = {};
    rhi::RhiExtent2D previewExtent = {};
    rhi::RhiFormat sourceFormat = rhi::RhiFormat::Unknown;
    std::shared_ptr<FrameGraphDebugPreviewTexture> texture;
};

struct FrameGraphDebugResourceVersion
{
    std::optional<UInt32> producer;
    std::vector<UInt32> readers;
    std::optional<UInt32> firstCompiledUse;
    std::optional<UInt32> lastCompiledUse;
    bool exported = false;
    FrameGraphDebugPreview preview;
};

struct FrameGraphDebugTexture
{
    std::string name;
    FrameGraphTextureDesc desc = {};
    bool imported = false;
    bool swapchain = false;
    std::vector<FrameGraphDebugResourceVersion> versions;
};

struct FrameGraphDebugBuffer
{
    std::string name;
    UInt64 size = 0;
    std::vector<FrameGraphDebugResourceVersion> versions;
};

struct FrameGraphDebugData
{
    UInt64 requestId = 0;
    UInt64 frameIndex = 0;
    Float32 previewScale = 0.5F;
    std::vector<FrameGraphDebugPass> passes;
    std::vector<FrameGraphDebugTexture> textures;
    std::vector<FrameGraphDebugBuffer> buffers;
    std::vector<FrameGraphDebugDependency> dependencies;
    std::vector<UInt32> executionPassIndices;
};
```

Forward-declare `FrameGraphDebugPreviewTexture` in this header. The data is mutable only while recording and is exposed as `shared_ptr<const FrameGraphDebugData>` after publication.

Implement:

```cpp
bool IsFrameGraphDebugPreviewScaleValid(Float32 scale) noexcept
{
    return std::isfinite(scale) && scale >= 0.1F && scale <= 1.0F;
}

rhi::RhiExtent2D CalculateFrameGraphDebugPreviewExtent(UInt32 width, UInt32 height, Float32 scale) noexcept
{
    const auto scaled = [scale](UInt32 value)
    {
        return (std::max)(1U, static_cast<UInt32>(std::lround(static_cast<Float32>(value) * scale)));
    };
    return {scaled(width), scaled(height)};
}
```

- [ ] **Step 5: Build and run the test**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
Build/windows-msvc-debug/Debug/VEngineFrameGraphDebugTests.exe
```

Expected: `VEngineFrameGraphDebugTests passed`.

- [ ] **Step 6: Commit**

```powershell
git add Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp Tests/Unit/FrameGraphDebugTests.cpp CMake/Targets/Engine.cmake CMake/Targets/Tests.cmake CMake/Targets/Tests/FrameGraphDebugTests.cmake
git commit -m "render: add frame graph debug data model"
```

## Task 3: Implement One-Shot Cross-Thread Exchange

**Files:**

- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp`
- Modify: `Tests/Unit/FrameGraphDebugTests.cpp`

- [ ] **Step 1: Write failing request and publication tests**

```cpp
bool TestCaptureExchange()
{
    ve::FrameGraphDebugCaptureExchange exchange;
    bool passed = true;
    passed &= Expect(exchange.RequestCapture(0.5F) == ve::ErrorCode::None, "first request should arm");
    passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Armed, "request should be armed");
    const auto request = exchange.ConsumeRequest();
    passed &= Expect(request.has_value(), "next render frame should consume the request");
    passed &= Expect(!exchange.ConsumeRequest().has_value(), "request should only be consumed once");
    passed &= Expect(exchange.GetStatus() == ve::FrameGraphDebugCaptureStatus::Capturing, "consume should enter capturing");

    auto data = std::make_shared<ve::FrameGraphDebugData>();
    data->requestId = request->requestId;
    exchange.Publish(data);
    auto transferred = exchange.TakePublishedData();
    passed &= Expect(transferred == data, "panel should receive the published shared pointer");
    passed &= Expect(exchange.TakePublishedData() == nullptr, "published data should transfer only once");
    return passed;
}
```

Also test invalid scales, a second request while Armed/Capturing, failure text, and a new request after Ready/Failed.

- [ ] **Step 2: Run and verify failure**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
```

Expected: compile failure because `FrameGraphDebugCaptureExchange` is not defined.

- [ ] **Step 3: Implement the mutex-protected exchange**

Expose:

```cpp
class FrameGraphDebugCaptureExchange final : public NonCopyable
{
public:
    [[nodiscard]] ErrorCode RequestCapture(Float32 previewScale);
    [[nodiscard]] std::optional<FrameGraphDebugCaptureRequest> ConsumeRequest();
    void Publish(std::shared_ptr<const FrameGraphDebugData> data);
    void Fail(UInt64 requestId, std::string message);
    [[nodiscard]] std::shared_ptr<const FrameGraphDebugData> TakePublishedData();
    [[nodiscard]] FrameGraphDebugCaptureStatus GetStatus() const;
    [[nodiscard]] std::string GetFailureMessage() const;
    void Reset() noexcept;
private:
    mutable std::mutex mutex_;
    UInt64 nextRequestId_ = 1;
    std::optional<FrameGraphDebugCaptureRequest> pendingRequest_;
    std::shared_ptr<const FrameGraphDebugData> publishedData_;
    FrameGraphDebugCaptureStatus status_ = FrameGraphDebugCaptureStatus::Idle;
    std::string failureMessage_;
};
```

`Publish` accepts only the currently capturing request ID, changes status to Ready, and replaces no panel-owned history because `TakePublishedData` moves the pointer out.

- [ ] **Step 4: Run the focused test**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
Build/windows-msvc-debug/Debug/VEngineFrameGraphDebugTests.exe
```

Expected: all exchange tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp Tests/Unit/FrameGraphDebugTests.cpp
git commit -m "render: add one shot frame graph capture exchange"
```

## Task 4: Build Original-Graph Snapshots And Capture Plans

**Files:**

- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp`
- Modify: `Tests/Unit/FrameGraphDebugTests.cpp`

- [ ] **Step 1: Write failing graph conversion tests**

Construct a `FrameGraphDebugSourceGraph` containing an imported texture version zero, retained producer/reader/writer passes, a culled producer, a buffer version, and an uninitialized transient texture zero. Assert:

```cpp
const ve::FrameGraphDebugBuildResult result = ve::BuildFrameGraphDebugData(source, {7, 0.5F});
passed &= Expect(result.data->passes.size() == source.passes.size(), "snapshot should retain culled passes");
passed &= Expect(result.data->passes[2].compiledIndex == std::nullopt, "culled pass should not have a compiled index");
passed &= Expect(ContainsHazard(result.data->dependencies, ve::FrameGraphDebugDependencyHazard::Raw), "snapshot should report RAW");
passed &= Expect(ContainsHazard(result.data->dependencies, ve::FrameGraphDebugDependencyHazard::War), "snapshot should report WAR");
passed &= Expect(ContainsHazard(result.data->dependencies, ve::FrameGraphDebugDependencyHazard::Waw), "snapshot should report WAW");
passed &= Expect(result.capturePlan.size() == 3, "only initialized versions with retained producers or valid imports should be captured");
passed &= Expect(result.data->textures[uninitializedIndex].versions[0].preview.state == ve::FrameGraphDebugPreviewState::Unavailable,
                 "uninitialized version zero should be unavailable");
passed &= Expect(result.data->textures[culledIndex].versions[1].preview.message == "producer culled",
                 "culled output should not execute only for capture");
```

- [ ] **Step 2: Run and verify the missing builder failure**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
```

Expected: compile failure naming `FrameGraphDebugSourceGraph` and `BuildFrameGraphDebugData`.

- [ ] **Step 3: Implement source records and pure conversion**

Define source records that mirror FrameGraph registration state without callbacks or RHI ownership:

```cpp
struct FrameGraphDebugSourceVersion
{
    UInt32 producer = InvalidFrameGraphDebugPassIndex;
    std::vector<UInt32> readers;
    UInt32 nextWriter = InvalidFrameGraphDebugPassIndex;
};

struct FrameGraphDebugCapturePlanEntry
{
    UInt32 textureIndex = InvalidFrameGraphResourceIndex;
    UInt32 version = 0;
    UInt32 captureBeforePass = InvalidFrameGraphDebugPassIndex;
    rhi::RhiExtent2D previewExtent = {};
    bool swapchain = false;
    bool requiresSampleableStaging = false;
};
```

`BuildFrameGraphDebugData` must:

1. Copy every original pass/resource/version.
2. Map retained registration indices to compiled indices.
3. Emit RAW producer-to-reader edges.
4. Emit WAR reader-to-next-writer edges.
5. Emit WAW producer-to-next-writer edges.
6. Deduplicate identical edges while preserving deterministic resource/version ordering.
7. Calculate per-version first/last compiled use.
8. Plan previews only for imported non-swapchain version zero and versions whose producer is retained.
9. Mark swapchain version zero, transient version zero, and culled-producer versions unavailable with explicit reasons.

- [ ] **Step 4: Run focused tests**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
Build/windows-msvc-debug/Debug/VEngineFrameGraphDebugTests.exe
```

Expected: snapshot, hazard, lifetime, deterministic ordering, and capture-plan tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp Tests/Unit/FrameGraphDebugTests.cpp
git commit -m "render: build frame graph debug snapshots"
```

## Task 5: Add Texture And Swapchain Copy Operations To RHI

**Files:**

- Modify: `Engine/RHI/Common/RhiDevice.h`
- Modify: `Engine/RHI/D3D11/D3D11Rhi.cpp`
- Modify: `Engine/RHI/D3D12/D3D12Rhi.cpp`
- Modify: `Engine/RHI/Metal/MetalRhi.mm`

- [ ] **Step 1: Add the common command-list contract**

Add next to `CopyTextureToSwapchain`:

```cpp
[[nodiscard]] virtual bool CopyTexture(RhiTexture& sourceTexture, RhiTexture& destinationTexture) = 0;
[[nodiscard]] virtual bool CopySwapchainToTexture(RhiSwapchain& swapchain, RhiTexture& destinationTexture) = 0;
```

Both calls require identical dimensions and format. They reject the same object and active incompatible render-pass state.

- [ ] **Step 2: Build to expose every unimplemented backend**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngine
```

Expected: compile failures naming the new pure virtual methods on D3D11 and D3D12. Metal will be verified by its platform build path.

- [ ] **Step 3: Implement D3D11 copies**

After shared compatibility checks, unbind render and shader-resource targets and record:

```cpp
context_->CopyResource(destination->GetNativeTexture(), source->GetNativeTexture());
context_->CopyResource(destination->GetNativeTexture(), swapchain->GetCurrentBackBuffer());
```

Return false for type, extent, or format mismatch.

- [ ] **Step 4: Implement D3D12 copies**

For texture-to-texture, transition tracked source/destination states to `COPY_SOURCE`/`COPY_DEST`, call `CopyResource`, and update both `D3D12Texture` state fields. For swapchain-to-texture:

```cpp
TransitionResource(swapchain->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
TransitionResource(destination->GetNativeResource(), destination->GetResourceState(), D3D12_RESOURCE_STATE_COPY_DEST);
commandList_->CopyResource(destination->GetNativeResource(), swapchain->GetCurrentRenderTarget());
destination->SetResourceState(D3D12_RESOURCE_STATE_COPY_DEST);
TransitionResource(swapchain->GetCurrentRenderTarget(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
```

- [ ] **Step 5: Implement Metal copies**

Use one `MTLBlitCommandEncoder` and `copyFromTexture`. `CopySwapchainToTexture` requires the command list's retained `drawable_`; if no drawable has been acquired for the logical swapchain producer, return false rather than acquiring undefined version-zero contents.

- [ ] **Step 6: Build both Windows backends**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngine
```

Expected: `VEngine` builds successfully with D3D11 and D3D12 implementations.

- [ ] **Step 7: Commit**

```powershell
git add Engine/RHI/Common/RhiDevice.h Engine/RHI/D3D11/D3D11Rhi.cpp Engine/RHI/D3D12/D3D12Rhi.cpp Engine/RHI/Metal/MetalRhi.mm
git commit -m "rhi: add texture capture copy operations"
```

## Task 6: Implement Persistent Preview Textures And Conversion Renderer

**Files:**

- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h`
- Create: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.cpp`
- Modify: `CMake/Targets/Engine.cmake`
- Modify: `Tests/Unit/FrameGraphDebugTests.cpp`

- [ ] **Step 1: Write failing format-mode and staging tests**

```cpp
passed &= Expect(ve::SelectFrameGraphDebugPreviewMode(ve::rhi::RhiFormat::Depth32Float) == ve::FrameGraphDebugPreviewMode::Depth,
                 "depth should use grayscale conversion");
passed &= Expect(ve::SelectFrameGraphDebugPreviewMode(ve::rhi::RhiFormat::R32Uint) == ve::FrameGraphDebugPreviewMode::UnsignedInteger,
                 "R32Uint should use deterministic false color");
passed &= Expect(ve::NeedsFrameGraphDebugStaging(ve::rhi::RhiTextureUsage::RenderTarget),
                 "a non-sampled render target should use staging");
passed &= Expect(!ve::NeedsFrameGraphDebugStaging(ve::rhi::RhiTextureUsage::RenderTarget | ve::rhi::RhiTextureUsage::Sampled),
                 "a sampled render target should convert directly");
```

- [ ] **Step 2: Run and verify failure**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
```

Expected: missing preview-mode API compile errors.

- [ ] **Step 3: Implement the render-thread preview owner**

```cpp
class FrameGraphDebugPreviewTexture final : public NonCopyable
{
public:
    [[nodiscard]] ErrorCode Initialize(rhi::RhiDevice& device, rhi::RhiExtent2D extent, std::string debugName);
    void Reset(std::vector<std::unique_ptr<rhi::RhiObject>>& retiredResources) noexcept;
    [[nodiscard]] rhi::RhiTexture* GetTexture() noexcept;
    [[nodiscard]] void* GetNativeSampledViewHandle() const noexcept;
private:
    std::unique_ptr<rhi::RhiTexture> texture_;
    std::atomic<void*> sampledView_{nullptr};
};
```

Create the texture as `Rgba8Unorm` with `Sampled | RenderTarget`. `Reset` runs only on the render thread, clears the atomic handle, and moves the texture into `pendingRetiredResources`.

- [ ] **Step 4: Implement conversion shaders and pipelines**

Use a fullscreen triangle generated from vertex ID. Provide separate typed fragment entry points:

```hlsl
Texture2D<float4> ColorTexture : register(t0);
Texture2D<float> DepthTexture : register(t0);
Texture2D<uint> UintTexture : register(t0);
SamplerState PreviewSampler : register(s0);

float4 PSColor(VSOutput input) : SV_TARGET { return ColorTexture.SampleLevel(PreviewSampler, input.uv, 0.0f); }
float4 PSDepth(VSOutput input) : SV_TARGET
{
    float depth = saturate(DepthTexture.SampleLevel(PreviewSampler, input.uv, 0.0f));
    return float4(depth, depth, depth, 1.0f);
}
float4 PSUint(VSOutput input) : SV_TARGET
{
    uint width;
    uint height;
    UintTexture.GetDimensions(width, height);
    uint2 coordinate = min(uint2(input.uv * float2(width, height)), uint2(width - 1u, height - 1u));
    uint value = UintTexture.Load(int3(coordinate, 0));
    if (value == 0) return float4(0.0f, 0.0f, 0.0f, 1.0f);
    value ^= value >> 16; value *= 0x7feb352du; value ^= value >> 15; value *= 0x846ca68bu; value ^= value >> 16;
    return float4(float3(value & 255u, (value >> 8) & 255u, (value >> 16) & 255u) / 255.0f, 1.0f);
}
```

Provide equivalent complete MSL entry points. Compile/cache shaders and three `Rgba8Unorm` graphics pipelines through `ShaderManager`; bind the source texture and a point-clamp sampler, then `Draw(3, 0)`.

Expose conversion recording as `ErrorCode RecordConversion(...)`. Shader, pipeline, binding, or draw preparation failures return an error and update the affected `FrameGraphDebugPreview` to `Failed`; they do not assert, terminate, or change the normal frame result.

- [ ] **Step 5: Run tests and build the engine**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests VEngine
Build/windows-msvc-debug/Debug/VEngineFrameGraphDebugTests.exe
```

Expected: focused tests pass and conversion renderer compiles.

- [ ] **Step 6: Commit**

```powershell
git add Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebugPreview.cpp Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp CMake/Targets/Engine.cmake Tests/Unit/FrameGraphDebugTests.cpp
git commit -m "render: add frame graph texture preview conversion"
```

## Task 7: Snapshot The Original FrameGraph And Inject Hidden Capture Passes

**Files:**

- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp`
- Modify: `Engine/Runtime/Render/Renderer/BaseRenderer.cpp`
- Modify: `Engine/Runtime/Render/RenderFramePipelineData.h`
- Modify: `Tests/Unit/FrameGraphDebugTests.cpp`

- [ ] **Step 1: Add failing tests for original versus instrumented ordering**

Extend the source-graph test with two writes of one physical texture and assert the capture plan adds the conceptual ordering `producer(v1) -> capture(v1) -> writer(v2)`, while `FrameGraphDebugData::passes` and `compiledPassOrder` retain only original pass indices. Also assert that no capture entry is generated for a culled producer.

```cpp
passed &= Expect(HasCaptureBeforeWriter(result.capturePlan, textureIndex, 1, writerPass),
                 "version one must be captured before version two overwrites its backing");
passed &= Expect(result.data->passes.size() == originalPassCount, "hidden passes must not appear in debug data");
passed &= Expect(!PlanContainsVersion(result.capturePlan, culledTextureIndex, 1), "culled output must not be revived");
```

- [ ] **Step 2: Run and verify the new ordering test fails**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
Build/windows-msvc-debug/Debug/VEngineFrameGraphDebugTests.exe
```

Expected: test failure at `HasCaptureBeforeWriter` until capture constraints are emitted.

- [ ] **Step 3: Add the frame-local capture context**

```cpp
struct FrameGraphDebugFrameCapture
{
    FrameGraphDebugCaptureRequest request;
    std::shared_ptr<FrameGraphDebugData> data;
    std::string failureMessage;
};
```

Add `FrameGraphDebugFrameCapture* frameGraphDebugCapture = nullptr;` to `FrameRenderPipelineData`. It is owned by `RenderSystem::RenderMainSwapchainFrame` for the duration of recording and submission.

- [ ] **Step 4: Compile and freeze the original graph**

Add `FrameGraph::PrepareDebugCapture(FrameGraphDebugFrameCapture&)`. Its sequence is exact:

```cpp
Error originalCompile = CompileInternal();
if (!originalCompile.IsOk()) return originalCompile;
capture.data = BuildDebugDataFromCompiledOriginal(capture.request);
FrameGraphDebugBuildResult buildResult = BuildDebugDataFromCompiledOriginal(capture.request);
capture.data = std::move(buildResult.data);
std::vector<FrameGraphDebugCapturePlanEntry> plan = std::move(buildResult.capturePlan);
impl_->ResetCompileResults();
impl_->stage = FrameGraphStage::SettingUp;
InjectDebugCapturePasses(plan, *capture.data);
impl_->stage = FrameGraphStage::SetupComplete;
return Error();
```

`BuildDebugDataFromCompiledOriginal` translates `PassNode`, `TextureResourceNode`, `BufferResourceNode`, exports, and original `compiledPassOrder` into `FrameGraphDebugSourceGraph`, then returns `FrameGraphDebugBuildResult`. Its `data` member becomes the published object; its separate `capturePlan` member exists only through injection.

- [ ] **Step 5: Inject ordinary, staging, and swapchain capture paths**

For each planned version:

- Allocate a persistent RGBA8 `FrameGraphDebugPreviewTexture` and import it.
- If preview or staging allocation fails, mark only that version Failed and skip its hidden passes.
- If the source is sampleable, append one hidden raster conversion pass that reads the exact source version and writes the preview.
- If the source is not sampleable, append a hidden copy pass into a same-format sampleable transient staging texture, then convert it.
- If the source is swapchain, append a hidden `CopySwapchainToTexture` pass into same-format staging, then convert it.
- Mark the final hidden pass side-effecting so capture output is retained.
- Attach the preview owner and availability metadata to the corresponding debug version.
- In every hidden execute callback, convert a failed RHI copy or conversion return into that preview's failure message and return normally.

Because each hidden pass is recorded as a reader of the source logical version, the existing next write consumes all readers and creates the required `capture(version) -> next writer` dependency.

- [ ] **Step 6: Integrate BaseRenderer**

After `frameGraph.Setup` and before normal compilation:

```cpp
if (frameRenderData_->frameGraphDebugCapture != nullptr)
{
    Error debugPrepare = frameGraph.PrepareDebugCapture(*frameRenderData_->frameGraphDebugCapture);
    if (!debugPrepare.IsOk())
    {
        frameRenderData_->frameGraphDebugCapture->failureMessage = debugPrepare.GetMessage();
    }
}
Error compileResult = frameGraph.Compile();
```

After Execute, copy actual original execution names into the data while filtering internal pass names by their explicit internal flag rather than by string prefix.

- [ ] **Step 7: Run focused tests and engine build**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests VEngine
Build/windows-msvc-debug/Debug/VEngineFrameGraphDebugTests.exe
```

Expected: original/instrumented separation and ordering tests pass; engine builds.

- [ ] **Step 8: Commit**

```powershell
git add Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraph.cpp Engine/Runtime/Render/Renderer/BaseRenderer.cpp Engine/Runtime/Render/RenderFramePipelineData.h Tests/Unit/FrameGraphDebugTests.cpp
git commit -m "render: inject frame graph debug capture passes"
```

## Task 8: Publish After Submit And Retire Panel-Owned Data Safely

**Files:**

- Modify: `Engine/Runtime/Render/RenderSystem.h`
- Modify: `Engine/Runtime/Render/RenderSystem.cpp`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h`
- Modify: `Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp`
- Modify: `Tests/Unit/FrameGraphDebugTests.cpp`

- [ ] **Step 1: Extend exchange tests for submit gating and retirement**

Add a pure submit-gating helper test:

```cpp
auto data = std::make_shared<ve::FrameGraphDebugData>();
passed &= Expect(!ve::ShouldPublishFrameGraphDebugCapture(ve::ErrorCode::PlatformError, data, {}), "failed submit must not publish");
passed &= Expect(!ve::ShouldPublishFrameGraphDebugCapture(ve::ErrorCode::None, nullptr, {}), "missing data must not publish");
passed &= Expect(!ve::ShouldPublishFrameGraphDebugCapture(ve::ErrorCode::None, data, "conversion failed"), "capture failure must not publish");
passed &= Expect(ve::ShouldPublishFrameGraphDebugCapture(ve::ErrorCode::None, data, {}), "successful submitted capture should publish");
```

Add `CollectFrameGraphDebugPreviewTextures` coverage that inserts the same preview owner into two version records and asserts the returned retirement list contains it exactly once.

- [ ] **Step 2: Run and verify failure**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
```

Expected: missing submit-gating helper failure.

- [ ] **Step 3: Add RenderSystem public APIs**

```cpp
[[nodiscard]] ErrorCode RequestFrameGraphDebugCapture(Float32 previewScale);
[[nodiscard]] FrameGraphDebugCaptureStatus GetFrameGraphDebugCaptureStatus() const;
[[nodiscard]] std::string GetFrameGraphDebugCaptureFailure() const;
[[nodiscard]] std::shared_ptr<const FrameGraphDebugData> TakeFrameGraphDebugData();
void RetireFrameGraphDebugData(std::shared_ptr<const FrameGraphDebugData> data);
```

`RequestFrameGraphDebugCapture` is scene-thread callable and delegates to the mutex exchange. `TakeFrameGraphDebugData` moves the published pointer to the panel.

Implement the tested gate as:

```cpp
bool ShouldPublishFrameGraphDebugCapture(ErrorCode submitResult,
                                         const std::shared_ptr<FrameGraphDebugData>& data,
                                         std::string_view failureMessage) noexcept
{
    return submitResult == ErrorCode::None && data != nullptr && failureMessage.empty();
}
```

- [ ] **Step 4: Consume before recording and publish after successful submit**

In `RenderMainSwapchainFrame`:

```cpp
std::optional<FrameGraphDebugCaptureRequest> request = impl.frameGraphDebugCapture.ConsumeRequest();
FrameGraphDebugFrameCapture debugCapture;
if (request.has_value())
{
    debugCapture.request = *request;
    frameData.frameGraphDebugCapture = &debugCapture;
}
framePipeline->RenderFrame(frameData);
const ErrorCode submitResult = SubmitMainSwapchainFrame(impl, frameData, framePipeline);
RequireRenderSystemFrameSuccess(submitResult, "RenderSystem failed to submit the frame", impl.device.get());
if (request.has_value())
{
    if (debugCapture.data != nullptr && debugCapture.failureMessage.empty())
        impl.frameGraphDebugCapture.Publish(std::move(debugCapture.data));
    else
        impl.frameGraphDebugCapture.Fail(request->requestId, std::move(debugCapture.failureMessage));
}
```

Keep publishing before Present so the contract is queue-submission success, as approved.

- [ ] **Step 5: Implement explicit panel-data retirement**

`RetireFrameGraphDebugData` enqueues a render command that obtains mutable preview pointers from the const data, calls `Reset(impl.pendingRetiredResources)` for each one, and then releases the shared pointer on the render thread. On `ShutdownDevice`, drain unpublished exchange data and pending retirement commands before destroying the RHI device.

- [ ] **Step 6: Run tests and build**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests VEngine
Build/windows-msvc-debug/Debug/VEngineFrameGraphDebugTests.exe
```

Expected: submit gating, shared-pointer transfer, and single-retirement tests pass.

- [ ] **Step 7: Commit**

```powershell
git add Engine/Runtime/Render/RenderSystem.h Engine/Runtime/Render/RenderSystem.cpp Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.h Engine/Runtime/Render/Renderer/FrameGraph/FrameGraphDebug.cpp Tests/Unit/FrameGraphDebugTests.cpp
git commit -m "render: publish submitted frame graph captures"
```

## Task 9: Add Testable Panel State And Capture/Pause Rules

**Files:**

- Create: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h`
- Create: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.cpp`
- Modify: `Tests/Unit/FrameGraphDebugTests.cpp`
- Modify: `CMake/Targets/Tests/FrameGraphDebugTests.cmake`

- [ ] **Step 1: Write failing Play/Pause eligibility tests**

```cpp
bool TestPanelCaptureRules()
{
    bool passed = true;
    passed &= Expect(!ve::editor::CanCaptureFrameGraph(false, ve::FrameGraphDebugCaptureStatus::Idle),
                     "Editing Mode must disable capture");
    passed &= Expect(ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Idle),
                     "Play Mode must enable idle capture");
    passed &= Expect(!ve::editor::CanCaptureFrameGraph(true, ve::FrameGraphDebugCaptureStatus::Capturing),
                     "an in-flight capture must disable another request");
    passed &= Expect(ve::editor::ShouldPauseAfterFrameGraphCapture(true, false, ve::ErrorCode::None),
                     "a successful request should pause a running Play session");
    passed &= Expect(!ve::editor::ShouldPauseAfterFrameGraphCapture(true, true, ve::ErrorCode::None),
                     "an already paused Play session needs no toggle");
    passed &= Expect(!ve::editor::ShouldPauseAfterFrameGraphCapture(true, false, ve::ErrorCode::InvalidState),
                     "a rejected request must not pause");
    return passed;
}
```

- [ ] **Step 2: Run and verify failure**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
```

Expected: compile failure naming the panel-model helpers.

- [ ] **Step 3: Implement pure panel helpers and stable IDs**

```cpp
bool CanCaptureFrameGraph(bool isPlaying, FrameGraphDebugCaptureStatus status) noexcept
{
    return isPlaying && status != FrameGraphDebugCaptureStatus::Armed && status != FrameGraphDebugCaptureStatus::Capturing;
}

bool ShouldPauseAfterFrameGraphCapture(bool isPlaying, bool isPaused, ErrorCode requestResult) noexcept
{
    return isPlaying && !isPaused && requestResult == ErrorCode::None;
}
```

Implement non-overlapping 64-bit ID ranges for pass nodes, texture pins, buffer pins, and dependency links. Add round-trip and collision tests across the maximum 32-bit index/version values accepted by the debug data model.

- [ ] **Step 4: Run tests**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
Build/windows-msvc-debug/Debug/VEngineFrameGraphDebugTests.exe
```

Expected: Play/Pause and stable-ID tests pass.

- [ ] **Step 5: Commit**

```powershell
git add Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.h Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanelModel.cpp Tests/Unit/FrameGraphDebugTests.cpp CMake/Targets/Tests/FrameGraphDebugTests.cmake
git commit -m "editor: define frame graph capture panel behavior"
```

## Task 10: Implement The Frame Graph Debug Panel

**Files:**

- Create: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.h`
- Create: `Editor/Panels/FrameGraphDebugPanel/FrameGraphDebugPanel.cpp`
- Modify: `CMake/Targets/Applications/Common.cmake`
- Modify: `Editor/Core/EditorProjectEditingView.h`
- Modify: `Editor/Core/EditorProjectEditingView.cpp`
- Modify: `Editor/Core/Editor.cpp`

- [ ] **Step 1: Add the panel shell and lifecycle**

The class owns exactly one visible result:

```cpp
class FrameGraphDebugPanel final : public BasePanel
{
public:
    void Init(Editor& editor) override;
    void Shutdown();
private:
    const char* GetName() const noexcept override;
    void RenderContent() override;
    Editor* editor_ = nullptr;
    ax::NodeEditor::EditorContext* nodeEditorContext_ = nullptr;
    std::shared_ptr<const FrameGraphDebugData> debugData_;
    Float32 previewScale_ = 0.5F;
};
```

`Shutdown` passes `debugData_` to `RenderSystem::RetireFrameGraphDebugData`, destroys the node-editor context, and clears `editor_`. Call it from a new `ProjectEditingView::Shutdown()` on `SceneSystemEditorCallback::onShutdown`, before the Scene Thread id is unregistered and before `Editor::ShutdownRenderBackend()` deletes the view. The Scene Thread's final RenderSystem flush drains the retirement command.

- [ ] **Step 2: Implement toolbar capture semantics**

Use `CanCaptureFrameGraph(editor_->IsPlaying(), status)` for button enablement. On click:

```cpp
const ErrorCode result = editor_->GetRenderSystem().RequestFrameGraphDebugCapture(previewScale_);
if (ShouldPauseAfterFrameGraphCapture(editor_->IsPlaying(), editor_->IsPaused(), result))
{
    editor_->TogglePause();
}
```

Clamp the slider to `[0.1F, 1.0F]`. Poll `TakeFrameGraphDebugData`; when non-null, retire the previous pointer through RenderSystem before assigning the new one. Display Idle/Armed/Capturing/Ready/Failed and the failure message without discarding the old successful data.

- [ ] **Step 3: Implement the upper node topology**

Create a top child window taking approximately 60% of available height. Within `ax::NodeEditor::Begin`:

- Emit one node per visible pass.
- Emit input/output pins grouped by texture/buffer access and version.
- Use distinct colors for raster, compute, culled, texture, buffer, RAW, WAR, and WAW.
- Emit links from `FrameGraphDebugDependency` using stable IDs.
- Handle selection and `NavigateToSelection` for table double-click requests.
- Render resource name and version beside pins; render hazard type in the selected-link details because node-editor links do not carry free text reliably at every zoom level.

- [ ] **Step 4: Implement lower tables and details**

Use a horizontal split beneath the graph. The left child contains Passes, Resources, and Dependencies tabs using ImGui tables with sortable registration/compiled indices, name search, kind filters, and Show Culled. The right child prints the selected record's complete metadata.

For a Ready texture preview:

```cpp
void* handle = preview.texture->GetNativeSampledViewHandle();
if (handle != nullptr)
{
    const ImTextureRef textureRef(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(handle)));
    ImGui::Image(textureRef, scaledDisplaySize);
}
```

Implement Fit and 1:1 buttons, mouse-wheel zoom clamped to a usable range, and middle/right-drag pan. Display explicit unavailable or failed messages for version zero, culled producers, unsupported formats, and allocation/conversion failures. Buffer details never show an Image control.

- [ ] **Step 5: Add the bottom tab and build both Editor targets**

Initialize the panel in `ProjectEditingView::Init`, add a `Frame Graph` item beside Console and Profile, and add all new sources to `VE_EDITOR_COMMON_SOURCES`.

Run:

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineWinEditor
```

Expected: the Editor links Dear ImGui and imgui-node-editor and builds without unresolved node-editor symbols.

- [ ] **Step 6: Commit**

```powershell
git add Editor/Panels/FrameGraphDebugPanel CMake/Targets/Applications/Common.cmake Editor/Core/EditorProjectEditingView.h Editor/Core/EditorProjectEditingView.cpp Editor/Core/Editor.cpp
git commit -m "editor: add frame graph debug panel"
```

## Task 11: Verify Backend Capture And Failure Behavior

**Files:**

- Modify only files whose behavior fails the checks in this task.

- [ ] **Step 1: Run formatting and static diff checks**

Run the repository formatter on changed C++ files, then:

```powershell
git diff --check
```

Expected: no whitespace errors.

- [ ] **Step 2: Run the focused unit executable and CTest entry**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug --target VEngineFrameGraphDebugTests
Build/windows-msvc-debug/Debug/VEngineFrameGraphDebugTests.exe
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineFrameGraphDebugTests --output-on-failure
```

Expected: direct executable and CTest both pass.

- [ ] **Step 3: Run the complete Windows build and test suite**

```powershell
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests --output-on-failure
```

Expected: complete build succeeds and every registered test passes.

- [ ] **Step 4: Smoke-test D3D12 Editor capture**

Launch deterministically:

```powershell
Build/windows-msvc-debug/Debug/VEngineWinEditor.exe --project "D:\github-desktop\VEngine\DemoProject"
```

Verify manually:

1. Frame Graph tab opens without changing the scene.
2. Capture is disabled in Editing Mode.
3. Start Play; Capture becomes enabled.
4. Set scale to `0.5`, click Capture, and confirm Play becomes Paused.
5. The next rendered frame reaches Ready and remains frozen.
6. Pass nodes, culled nodes, RAW/WAR/WAW relationships, tables, and selection navigation agree.
7. Color, raw-depth grayscale, `R32Uint` false color, multi-version resources, and final swapchain versions have previews.
8. Uninitialized transient version zero and buffers show the designed metadata-only state.
9. Resume Play; the displayed capture remains unchanged until another Capture.

- [ ] **Step 5: Smoke-test D3D11 and compile-check Metal**

Start the Editor with its existing D3D11 backend selection mechanism and repeat the capture checks. On macOS, run the configured Mac Editor build and confirm `FrameGraphDebugPreview.cpp` and `MetalRhi.mm` compile with the complete MSL and swapchain-copy implementation.

- [ ] **Step 6: Re-run verification after any correction**

For each failure, apply the systematic-debugging workflow, add or tighten the focused regression assertion first where the defect is testable, then repeat Steps 1 through 5. Do not weaken capture coverage to make the smoke test pass.

- [ ] **Step 7: Commit final verified corrections**

```powershell
git add -A
git commit -m "test: verify frame graph debugger capture"
```

If verification required no source correction, omit this empty commit.

## Task 12: Final Review And Handoff

**Files:**

- Review all changed files and both design/plan documents.

- [ ] **Step 1: Confirm scope and ownership**

Check that GPU timing and buffer readback were not added, the panel is the sole long-term owner of the visible `shared_ptr<const FrameGraphDebugData>`, and every replacement/shutdown path returns preview resources to RenderSystem.

- [ ] **Step 2: Confirm original-graph fidelity**

Check that the published pass/resource arrays were frozen before hidden injection, compiled indices reflect the original graph, culled producers are not revived, and internal passes/resources cannot pass UI filters because they are absent from the snapshot.

- [ ] **Step 3: Confirm capture semantics**

Check that Editing Mode disables capture, Play and Paused Play Mode allow it when no request is in flight, a successful click pauses an actively running session, and only the next submitted render frame publishes data.

- [ ] **Step 4: Inspect repository state**

```powershell
git status --short --branch
git log --oneline main..HEAD
```

Expected: only intentionally untracked local brainstorming artifacts may remain; all implementation changes are committed on `codex/framegraph-debugger`.

- [ ] **Step 5: Use verification-before-completion and requesting-code-review**

Run the required verification skill against fresh command output, then use the code-review skill to inspect architecture, thread ownership, RHI transitions, node ID stability, and the user workflow before reporting completion.
