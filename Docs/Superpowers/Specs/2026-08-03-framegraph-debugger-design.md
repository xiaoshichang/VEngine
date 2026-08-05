# FrameGraph Debugger Design

**Date:** 2026-08-03

## Goal

Add an Editor-only FrameGraph debugger that captures the next rendered frame on demand and presents an immutable view of that frame's original graph, resource versions, dependency relationships, execution metadata, and texture contents.

The workflow is deliberately one-shot:

1. Open the Frame Graph debug panel.
2. Choose a preview scale from `0.1` through `1.0` (default `0.5`).
3. Click **Capture Next Frame**.
4. The next frame records all graph metadata and all initialized logical texture versions.
5. The captured result remains frozen until another capture succeeds.

GPU timing and buffer-content readback are outside this feature.

## User Interface

The panel uses a vertical split:

- The upper, full-width region is an `imgui-node-editor` topology canvas.
- The lower-left region contains Pass, Resource, and Dependency tables.
- The lower-right region contains details and the selected texture-version preview.

The toolbar contains the preview scale, capture button, capture status, name search, pass/resource filters, and a toggle for showing culled passes.

**Capture Next Frame** is enabled only while the Editor is in Play Mode (`Editor::IsPlaying()`), including an already paused Play session. When the button successfully arms a capture while the scene is actively running, the panel immediately pauses Play Mode. VEngine continues rendering with zero simulation delta while paused, so the next rendered frame supplies the requested debug data while the scene remains frozen for inspection. If Play Mode was already paused, clicking Capture only arms the next render frame. Editing Mode never accepts a capture request.

Passes are topology nodes. Resource versions appear as typed input and output pins so the canvas remains compact. Links identify the resource name, logical version, and dependency hazard (`RAW`, `WAR`, or `WAW`). Culled passes remain available in a muted style. Hidden debugger passes and preview resources never appear in the displayed graph.

Selecting a pass displays its registration and compiled order, retained/culled state, type, side-effect status, attachments, load/store operations, accesses, barriers, and dependencies. Selecting a resource pin, link, or resource-table row displays the resource description, version, producer and readers, lifetime, and preview. Double-clicking a table entry locates it on the graph. The preview supports fit, 1:1, zoom, and pan.

Panel state is one of `Idle`, `Armed`, `Capturing`, `Ready`, or `Failed`. While a request is in progress, the previous successful capture stays visible and the capture button is disabled.

## Third-Party Node Editor

Dear ImGui remains at the repository's current version. The debugger adds [`thedmd/imgui-node-editor`](https://github.com/thedmd/imgui-node-editor) as an independent Editor-only dependency under `ThirdParty/`; it does not replace or fork Dear ImGui.

The dependency is pinned to a known revision. Until upstream incorporates the ImGui 1.92.8 compatibility changes, VEngine carries the narrow source adjustments represented by [upstream pull request 339](https://github.com/thedmd/imgui-node-editor/pull/339). The wrapper and download script follow the repository's existing `ThirdParty` and CMake conventions.

## Capture Ownership And Threading

`FrameGraphDebugPanel` owns the visible result as:

```cpp
std::shared_ptr<const FrameGraphDebugData> debugData_;
```

`FrameGraphDebugData` is immutable after publication and contains both the graph snapshot and preview texture handles. It has no pointers to the transient `FrameGraph` or its frame-local allocator.

A `FrameGraphDebugCaptureExchange` connects the Editor/main thread and render thread:

- The Editor publishes a one-shot `FrameGraphDebugCaptureRequest` containing a monotonically increasing request ID and preview scale.
- The render thread consumes a request at most once, on the next frame that builds a FrameGraph.
- After successful execution and submission, the render thread publishes a new `shared_ptr<const FrameGraphDebugData>`.
- The panel polls the exchange and replaces its current shared pointer only when a newer successful capture is available.
- The exchange transfers current/pending data but is not a historical-capture store.

The panel returns replaced snapshots to `RenderSystem`, which releases their preview textures on the Render Thread through fence-safe deferred destruction. During Editor shutdown, `SceneSystem` invokes the Editor shutdown callback before unregistering the Scene Thread id; the panel enqueues its final snapshot retirement there, and the following RenderSystem flush drains it before backend teardown.

## Captured Data Model

The value-only snapshot records:

- Capture request ID, rendered frame index, scale, status, and diagnostics.
- Original passes, including registration index, optional compiled index, type, retained/culled state, side-effect flag, raster attachments, accesses, barriers, and dependencies.
- Texture and buffer resources, including name, description, imported/transient/swapchain state, logical versions, producer/readers, exported state, and first/last use.
- Dependency edges, including before/after pass, resource/version, and hazard type.
- Per-texture-version preview metadata, availability or failure reason, source and preview dimensions, source format, and sampled preview handle.

Buffers expose names, sizes, logical versions, producers, readers, and access relationships. Their contents are not captured.

## FrameGraph Integration

Capture uses hidden internal FrameGraph passes because logical texture versions share physical backing and may be overwritten later in the same frame.

On a requested frame:

1. Renderers finish registering the original graph.
2. FrameGraph freezes a snapshot of the original declarations.
3. The debugger appends hidden capture passes for initialized texture versions.
4. The instrumented graph compiles and executes normally.
5. After a successful queue submission, the completed immutable debug data is published.

Each hidden pass is registered as a reader of the version it captures. Existing version-reader dependency construction consequently orders the capture before a subsequent writer can overwrite that physical texture. The displayed snapshot was taken before injection, so internal passes and resources do not contaminate the user's graph.

The instrumentation must not change the visible frame result. Preview failures are best-effort diagnostics: a failed preview is marked unavailable while topology data and normal rendering continue whenever possible.

## Texture Preview Conversion

Every initialized logical texture version receives a persistent `RGBA8` preview target. Its dimensions are:

```text
max(1, round(sourceWidth  * scale))
max(1, round(sourceHeight * scale))
```

Conversion is performed by hidden graphics passes using the existing shader-management path and backend-appropriate HLSL/Metal shaders:

- `Rgba8Unorm`, `Bgra8Unorm`, and floating-point color formats use normal color sampling.
- `Depth32Float` displays raw device depth in `[0, 1]` as grayscale. Camera-specific linearization is not attempted.
- `R32Uint` displays zero as black and hashes every non-zero integer into a deterministic false color. The same value therefore has the same color without requiring an additional min/max reduction.

An uninitialized transient version zero is not read and is reported as `Unavailable: uninitialized`. Unsupported formats or backend operations receive a per-preview failure reason.

## Swapchain Capture

The logical swapchain resource does not have an ordinary sampleable `RhiTexture`. The RHI therefore gains a narrowly scoped operation that copies the current swapchain image into a compatible same-sized temporary texture. A following hidden conversion pass samples that texture into the scaled persistent `RGBA8` preview.

The copy and conversion are ordered as capture readers for each initialized logical swapchain version. Undefined initial swapchain contents are never captured. Temporary resources use normal FrameGraph lifetime management; the final preview remains owned through `FrameGraphDebugData`.

## Error Handling

- The public request API rejects preview scales outside `[0.1, 1.0]`; the panel slider only emits valid values.
- A request is consumed only once.
- Per-resource allocation, conversion, or backend failures mark that preview unavailable without discarding usable graph data.
- An overall capture failure changes the panel status to `Failed`, reports the reason, and preserves the previous successful `FrameGraphDebugData`.
- If the underlying render frame itself cannot compile, execute, or submit, existing renderer failure policy remains authoritative.

## Testing And Verification

Focused automated tests cover:

- Capture eligibility in Editing, Playing, and Paused Editor states, including the pause action after successfully arming a running session.
- One-shot next-frame request consumption.
- Snapshot exclusion of hidden capture passes and resources.
- Capture ordering before a later writer overwrites a logical version.
- Skipping uninitialized transient version zero.
- Preview scale validation and rounded, clamped dimensions.
- Immutable shared-data publication and replacement behavior.
- Pass/resource/version/dependency metadata, including culled passes and hazard classification.

Backend and Editor verification covers:

- Full Windows Debug configure/build and relevant CTest execution through `CMake/Scripts/WithMsvc.bat`.
- D3D11 and D3D12 Editor smoke capture using `--project D:\github-desktop\VEngine\DemoProject`.
- Color, depth, `R32Uint`, multiple logical versions, and swapchain previews.
- Metal source and build-path compilation where available.

## Non-Goals

- GPU timestamp or duration profiling.
- Buffer-content capture.
- Continuous capture or frame history.
- Serialization/export of captures.
- Camera-aware depth linearization.
- Displaying debugger-injected passes as part of the original graph.
