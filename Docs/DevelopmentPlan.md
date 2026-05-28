# VEngine Development Plan

## 1. Purpose

This document describes the rough development plan for `VEngine`.

The plan is intentionally milestone-oriented. Each milestone should produce a usable vertical slice instead of only isolated low-level systems. The goal is to reduce architectural risk early, especially around the RHI design, shader cross-compilation, C# hosting, and iOS Metal integration.

## 2. Technical Feasibility Items To Validate Early

These items should be validated with small prototypes before the full systems are built.

### 2.1 D3D11, D3D12, And Metal RHI Shape

Goal:

- Confirm that the common RHI can map to D3D12 and Metal naturally.
- Confirm that D3D11 can emulate the required command and binding concepts.

Prototype:

```text
Create device
Create swapchain
Create buffer
Create texture
Create pipeline
Draw triangle
```

### 2.2 HLSL To Metal Shader Flow

Goal:

- Confirm HLSL source can produce D3D output and Metal-compatible MSL.

Prototype:

```text
HLSL
  -> DXC
    -> DXIL
    -> SPIR-V
  -> SPIRV-Cross
    -> MSL
    -> Reflection
```

### 2.3 C# Native Hosting On Windows

Goal:

- Confirm native C++ host can initialize .NET, load a script assembly, and call managed script lifecycle methods.

Prototype:

```text
C++ Player
  -> nethost / hostfxr
  -> Load C# assembly
  -> Call OnUpdate
  -> C# calls native logging API
```

### 2.4 iOS Metal Demo

Goal:

- Confirm CMake/Xcode target can run a native iOS Simulator app using Metal.

Prototype:

```text
iOS app
  -> UIKit lifecycle
  -> CAMetalLayer / MTKView
  -> Metal device
  -> Draw triangle
```

### 2.5 Boost.Log Integration

Goal:

- Confirm the engine logging facade can route logs to the Windows debug console, file sink, and optional callback path
  used by tests or tooling.

Prototype:

```text
VE_LOG_INFO
  -> Boost.Log core
  -> Debug console sink
  -> File sink
```

## 3. Development Roadmap

### Milestone 0: Project Skeleton

- Create CMake root project.
- Create `CMakePresets.json`.
- Create `ThirdParty/` dependency wrappers and download scripts.
- Create `VEngine` static library target.
- Create `VEnginePlayer`, `VEngineEditor`, and CTest-registered unit test executables.
- Integrate Boost and CMake/CTest-based tests.
- Document and enforce Visual Studio 2022 Build Tools/MSVC v143 as the first Windows compiler baseline for
  `windows-msvc-*` presets and repository-owned third-party dependency payloads.
- Establish initial folder structure only through files that are needed by current targets.
- Add basic coding style and build documentation.

### Milestone 1: Core, Logging, And Platform

- Implement `Core`.
- Implement assertion and error utilities.
- Implement `Boost.Log` based logging facade.
- Implement Windows Win32 window.
- Implement Windows message loop.
- Implement basic time system.
- Implement basic FileSystem.
- Run empty Windows Player window.
- Run empty Windows Editor window.

### Milestone 2: Memory, Math, And Threading

- Implement `PoolAllocator`.
- Implement cross-platform thread wrapper, synchronization primitives, and first lock-free utilities.
- Implement core math primitives: `Vector2`, `Vector3`, `Vector4`, `Matrix44`, and `Quaternion`.
- Implement Job System first version.
- Add `EngineRuntime` as the shared runtime service lifecycle layer for Player and Editor.
- Implement IOSystem first version.
- Add unit tests for Memory, Math, Threading, Job System, EngineRuntime, and IOSystem.

### Milestone 3: RHI Foundation

- Design common RHI.
- Implement D3D11RHI minimum path.
- Implement D3D12RHI minimum path.
- Add swapchain, buffer, texture, shader, pipeline, and command list concepts.
- Build initial RenderSystem with Render Thread and lock-free Render Command Queue.
- Connect RenderSystem lifecycle through `EngineRuntime`.
- Connect RHI device and swapchain lifecycle through RenderSystem after the service boundary is stable.

### Milestone 4: Shader Pipeline

- Integrate DirectXShaderCompiler.
- Compile HLSL to DXIL.
- Compile HLSL to SPIR-V.
- Integrate SPIRV-Cross.
- Generate MSL.
- Generate shader reflection metadata.
- Create `VEngineShaderTool`.
- Add shader compile smoke tests.

### Milestone 5: Scene And Rendering Vertical Slice

Detailed design:

- See [Scene And Rendering Vertical Slice](SceneRenderingVerticalSlice.md).

Implementation order:

- Add `GameThreadSystem` as the owner of the Game Thread execution context, frame counter, tick phases, and scene
  mutation rule.
- Start the first Player Game Thread in continuous mode so it owns its frame loop independently from the Main Thread.
- Define the first Game Thread tick phases: begin frame, lifecycle dispatch, update, late update, transform update,
  render extraction, and end frame.
- Connect `GameThreadSystem` lifecycle through `EngineRuntime` after JobSystem, IOSystem, ResourceManager, and
  RenderSystem are available.
- Implement `Scene`, `GameObject`, and `Component` base types with stable object identifiers, names, active/enabled
  state, hierarchy ownership, component storage, and lifecycle hooks.
- Implement `TransformComponent` with local transform, world transform, parent-child dirty propagation, and matrix
  extraction.
- Add debug validation or assertions for Game Thread-owned scene mutation APIs.
- Add first lifecycle and hierarchy tests before render-facing components are layered on top.
- Implement a minimal Reflection registry for type metadata, component factories, serializable properties, default
  values, editor visibility flags, and enum metadata.
- Register `TransformComponent` through Reflection and use reflected properties in tests.
- Implement `CameraComponent`, `MeshRendererComponent`, and directional `LightComponent` as render-facing scene
  components.
- Register render-facing components through Reflection.
- Define the first JSON scene serialization shape for scene metadata, GameObjects, hierarchy, components, and reflected
  property values. Leave the asset-level `.vescene` file contract and AssetDatabase integration to Milestone 6.
- Implement scene serialization and deserialization, including graceful skip and warning behavior for unknown component
  types.
- Add a scene serialization round-trip test that covers Transform, Camera, MeshRenderer, and Light components.
- Add typed resource handles and a first `ResourceManager` for mesh and material resources used by the sample scene.
  Keep texture and shader resource management as placeholders for the asset/material expansion.
- Add built-in fallback resources required by the vertical slice: cube mesh and default material. Default shader
  resources and placeholder textures are deferred until shader/material assets are introduced.
- Use code-constructed built-in resources for the first vertical slice. Route file-backed resource loading through
  FileSystem and IOSystem in the asset pipeline milestone.
- Keep built-in resources in a ResourceManager-owned catalog addressed by stable `builtin:` locators so project
  `Assets/` stays reserved for authored files.
- Define `SceneRenderSnapshot` or an equivalent render-safe frame packet containing camera, light, draw item, transform,
  and resource handles.
- Add Game Thread scene extraction that builds render snapshots without live `Scene`, `GameObject`, or `Component`
  pointers.
- Add `RenderSystem::SynchronizeRenderResources()` for resource add/update/remove propagation and
  `RenderSystem::SubmitFrame()` for render-safe scene snapshots.
- Add CPU-side frame backpressure through a Game Thread frame-end `RenderCommandFence`, with an initial one-frame-lag
  policy.
- Add render-side frame contexts for per-frame render data.
- Add the first scene-driven render path: extract camera, directional light, transforms, and draw resource handles into
  a render-safe snapshot; synchronize mesh/material add/update/remove changes into a persistent render-side resource
  registry; then draw static meshes through an MVP uniform buffer and the existing color pipeline.
- Defer indexed mesh buffers, depth buffer resources, full material binding, upload scheduling, and GPU-safe deferred
  release until the next RHI/render-resource expansion.
- Construct the sample scene in Windows Player, bind it to the Game Thread, and render it through the existing
  RenderSystem/RHI path. Scene file loading and automated Player window smoke remain follow-up work.
- Add focused RenderSystem tests for frame submission, queued-frame backpressure, render frame context selection, and
  render-resource synchronization. Current coverage is through GameThreadSystem, Scene, and ShaderTool tests;
  dedicated RenderSystem tests remain pending.

### Milestone 6: Asset Pipeline

Detailed design:

- See [Asset Pipeline](AssetPipeline.md).

Implementation order:

- Define stable `AssetGuid` handling for project assets using Boost.UUID, and keep `AssetGuid` distinct from runtime
  `ResourceId` values.
- Add first asset reference types for scene/component serialization so mesh and material references are stored as GUIDs
  with optional project-relative path fallbacks instead of runtime resource handles.
- Establish first project asset roots: versioned authored/source assets under `Assets/` and generated import artifacts
  under `Generated/Assets/`.
- Define the common JSON file contract rules for asset files: `format`, `version`, `guid`, `assetType`, project-relative
  paths, explicit version checks, and clear diagnostics for unsupported versions.
- Define `.veasset` source metadata sidecars with GUID, source path, source hash placeholder or content hash, importer
  id, importer version, import settings, generated artifacts, and dependency records.
- Promote the Milestone 5 scene serialization shape into the asset-level `.vescene` contract with scene metadata and
  asset-reference properties.
- Define `.vematerial` with shader reference, base color parameters, and texture slots. Use base color in the first
  renderer path and keep texture slots as dependency metadata until texture resource loading lands.
- Define first `.vemesh` native mesh payloads for imported static meshes. Keep the earliest payload text-friendly, and
  either expand indices during load or add indexed mesh support only if it remains a small render-resource change.
- Implement `AssetDatabase` as a simple in-memory index backed by per-asset text files rather than a central binary
  database.
- Add `AssetDatabase` project open, refresh, path normalization, GUID lookup, path lookup, artifact lookup, dependency
  lookup, duplicate GUID detection, and validation APIs.
- Add metadata creation and update paths for source assets and authored VEngine assets.
- Add atomic-enough save behavior for local development metadata writes.
- Keep source importers separate from Player runtime loading. Link source import code through `VEngineAssetTool` and the
  Windows Editor import path, not through runtime source loading.
- Implement a tiny project-owned OBJ importer first to prove the full source-to-runtime chain while the assimp
  third-party wrapper is prepared.
- Add the assimp importer backend for `fbx`, `gltf`, and `glb` source files after the first OBJ vertical slice is
  stable.
- Implement first import settings for unit scale, normal generation policy, and basic mesh combine or per-mesh artifact
  behavior.
- Import mesh positions, normals, vertex colors where present, material slots, and source names into `.vemesh`
  artifacts.
- Create or update simple `.vematerial` assets from imported material colors when practical.
- Track source material texture references as asset dependencies without requiring full texture decoding or GPU texture
  upload in this milestone.
- Write generated import artifacts to stable GUID-based paths under `Generated/Assets/ImportCache/`.
- Add deterministic reimport behavior based on source path, importer version, settings, and source hash when available.
- Expand `ResourceManager` with file-backed mesh and material loading APIs while keeping built-in fallback resources.
- Route runtime file reads through `IOSystem` and CPU parse/conversion work through `JobSystem` where the runtime path
  can be asynchronous.
- Apply completed resource loads to live runtime state at a controlled Game Thread boundary, then rely on existing
  `RenderSystem::SynchronizeRenderResources()` revision tracking for render-side updates.
- Add scene loading from `.vescene`, including GUID-based mesh/material resolution. Missing mesh artifacts fail scene
  loading, while missing material assets can warn and use the default material.
- Create `VEngineAssetTool` commands for `scan`, `import`, `reimport`, and `validate`.
- Make `VEngineAssetTool` initialize logging, open the project `AssetDatabase`, run the shared import pipeline, print CI
  friendly diagnostics, and return non-zero exit codes on failure.
- Add an Editor-triggered import backend path that calls the same import service as `VEngineAssetTool`. Keep full Asset
  Browser UI for Milestone 7.
- Add a file-backed sample scene under `Examples/AssetPipelineSample/Assets/Samples/` that references a material and
  imported static mesh asset.
- Update the Windows Player path so it can load the sample `.vescene` through the asset pipeline and render it through
  the existing scene/render vertical slice.
- Add focused CTest coverage for GUID parsing, asset path normalization, metadata round trips, AssetDatabase lookup,
  validation, material loading, scene asset loading, source-model import smoke, reimport stability, ResourceManager
  file-backed loading, and `VEngineAssetTool` command smoke behavior.

### Milestone 7: Editor MVP

Detailed design:

- See [Editor MVP](EditorMVP.md).

Implementation order:

- Define the first VEngine project descriptor `.veproject`, including project GUID, display name, engine version, target
  platforms, and startup scene reference.
- Keep the repository root as an engine workspace, not a project instance; put bundled editable projects under
  `Examples/`.
- Define and validate the project directory contract: versioned authored/source files under `Assets/`, disposable
  generated files under `Generated/`, and package staging under `Generated/Build/`.
- Add an Editor project service that owns the opened project root, parsed project descriptor, `AssetDatabase`, current
  edit scene, dirty state, and project diagnostics.
- Support direct project opening from `VEngineEditor.exe --project <project-root>`.
- When `VEngineEditor.exe` starts without a project argument, show a Project Launcher screen that lists recent projects
  and offers browse/create actions.
- Store the machine-local recent project list in the current user's Windows registry hive under
  `HKCU\Software\VEngine\Editor`, and update it after successful project opens.
- Keep `File -> Open Project...` available inside the Editor for switching projects.
- On project open, parse `.veproject`, create missing generated subdirectories, refresh `AssetDatabase`, surface
  diagnostics in the Windows debug console, and open the startup scene when available.
- Store any persisted local Editor layout or workspace state in user-local settings or `Generated/Editor/`, not in
  authored assets.
- Integrate Dear ImGui and add the Editor shell, main menu, status bar, project title display, and first Unity-style
  panel layout.
- Add Asset Browser over `AssetDatabase` scan results, including GUID, type, path, importer, generated artifact status,
  and commands for scan, import, reimport, and validate.
- Add scene open/save for `.vescene` assets through the existing scene serialization and asset reference contracts.
- Add Scene Hierarchy and Inspector, editing component properties through Reflection.
- Add Viewport rendering of the current edit scene through the existing RenderSystem/RHI path.
- Add Play / Stop using a separate play scene instance so runtime state does not mutate the edit scene.
- Define the first package command/service shape shared by Editor and tools. Stage Windows packages into a read-only
  `Content/` layout containing `.veproject`, an asset manifest, authored native assets, generated artifacts, shader
  outputs, and required runtime binaries.
- Document and structure the iOS package staging layout so Milestone 10 can place the same `Content/` contract inside
  the iOS app bundle after Metal shader artifacts are generated.
- Add focused tests for project descriptor parsing, project root validation, generated directory creation, asset browser
  backend commands, scene open/save, play scene isolation, and package manifest generation.

### Milestone 8: C# Scripting Windows MVP

Detailed design:

- See [C# Scripting Windows MVP](CSharpScriptingWindowsMVP.md).

Implementation order:

- Add CMake discovery for .NET native hosting and make scripting an always-built engine module, with Windows managed
  scripting targets built through the .NET SDK.
- Integrate `nethost` and `hostfxr` behind a small Windows-only `DotNetHost` wrapper with explicit diagnostics for
  missing SDK/runtime files, runtime config files, assemblies, and bootstrap entry points.
- Add platform stubs so non-Windows builds keep compiling until platform-specific script hosts are implemented.
- Create the handwritten managed `VEngine.ScriptAPI` project with minimal wrappers for `GameObject`, `Component`,
  `ScriptBehaviour`, `Transform`, `Time`, and `Log`.
- Add a C ABI `ScriptBridge` that exposes only opaque handles and narrow native calls for logging, time reads, object
  names, and Transform mutation.
- Add `ScriptHost` and `ScriptContext` to load one project script assembly, create managed script instances, dispatch
  lifecycle methods, catch managed exceptions, and tear down instances deterministically.
- Implement native `ScriptComponent`, register it through Reflection, and serialize `scriptTypeName` plus optional
  `assemblyName` in `.vescene` files.
- Dispatch `OnCreate`, `OnDestroy`, `OnEnable`, `OnDisable`, and `OnUpdate(float deltaTime)` through the existing scene
  lifecycle on the Game Thread.
- Extend `.veproject` with an optional script section that identifies the Windows C# project and assembly name without
  storing generated output paths in authored project data.
- Add generated script output folders under `Generated/Scripts/Windows/<Configuration>/` and route Editor build output
  there.
- Support Editor rebuild and script-context reload only after Play mode is stopped; do not preserve live managed state
  across reload.
- Bind Windows Player startup to the configured project script assembly when scripts are present, and leave
  `ScriptComponent` inert with warnings when scripting is disabled.
- Extend Windows package staging to include managed script artifacts under `Content/Scripts/Windows/` and validate
  missing or mismatched script payloads.
- Add focused tests for host initialization failure diagnostics, managed lifecycle order, bridge logging, time access,
  Transform mutation, `ScriptComponent` serialization, managed exception handling, and reload-after-stop behavior.
- Add a bundled sample script project that drives one simple object in the existing sample project.

### Milestone 9: Runtime UI And Lightweight Physics

- Implement Canvas.
- Implement RectTransform.
- Implement Image.
- Implement Label with FreeType.
- Implement Button.
- Implement UI event routing.
- Implement AABB, Sphere, and Raycast for picking and lightweight physics.
- Implement ColliderComponent.
- Add simple UI and picking demo.

### Milestone 10: iOS Simulator Demo

- Create iOS Player target.
- Add Objective-C++ platform bridge.
- Add UIKit app lifecycle.
- Add Metal surface integration.
- Implement MetalRHI minimum path.
- Use HLSL to MSL shader flow.
- Draw simple 3D demo in iOS Simulator.
- Add touch input demo.
- Keep C# gameplay disabled in this milestone; validate native iOS platform, Metal rendering, shader artifacts, and
  package `Content/` layout first.

### Future Milestone: iOS C# AOT Feasibility

Goal:

- Determine whether VEngine can support C# gameplay on iOS as an ahead-of-time compiled module without adopting
  Windows-style runtime script assembly loading.

Expected direction:

- Keep `ScriptComponent` as the cross-platform scene concept, but use a platform-specific iOS script host.
- Build project C# gameplay code during the iOS package/archive step instead of loading arbitrary managed DLLs at
  runtime.
- Compile managed code ahead of time for `iossimulator-arm64`, `iossimulator-x64`, and `ios-arm64` as needed.
- Generate or register a build-time script type table so serialized `scriptTypeName` values resolve to known AOT script
  factories.
- Link the AOT script output into `VEngineIOSPlayer.app` either statically or as an embedded framework, depending on
  what proves simplest for CMake/Xcode, code signing, and symbol/debug-file handling.
- Stage the normal VEngine `Content/` folder into the app bundle with project metadata, asset manifests, native assets,
  imported asset caches, and Metal shader artifacts.
- Do not package iOS gameplay as `Content/Scripts/Windows/*.dll`, `.deps.json`, or `.runtimeconfig.json`.
- Treat hot reload and runtime script replacement on iOS as non-goals unless a later Apple-compliant workflow is proven.
- Add simulator smoke coverage first, then device archive/signing/package validation once the simulator path is stable.

## 4. Recommended Implementation Order

Recommended high-level order:

```text
CMake skeleton
  -> Core / Boost.Log / FileSystem
  -> Windows Platform
  -> Memory / Math / Threading
  -> EngineRuntime / Job System / IOSystem / RenderSystem
  -> RHI common design
  -> D3D11 and D3D12 minimum rendering
  -> Shader pipeline
  -> Scene / Resource
  -> Editor MVP
  -> C# scripting
  -> Runtime UI / lightweight physics
  -> Metal / iOS Simulator
  -> iOS C# AOT feasibility
```

D3D11, D3D12, and Metal should be considered together during RHI design. However, each backend should be implemented through small smoke-tested vertical slices instead of attempting a complete renderer immediately.

The most important first-stage deliverable is a working engine loop that can open a window, run a multithread-aware frame, render a static mesh, load a simple scene, and be inspected through the Editor.

## 5. EngineRuntime Integration

`EngineRuntime` is the shared runtime service layer used by Player, Editor, tools, and future platform backends. It owns
the lifecycle of long-lived engine services and provides explicit access to them without requiring global singletons.

Initial services:

- Job System.
- IOSystem.
- RenderSystem.

Later services should connect through this layer as their modules land:

- Scene runtime state.
- ResourceManager and asset loading.
- Input, scripting, runtime UI, and lightweight physics.

Later RenderSystem work should attach RHI device, swapchain, render resource, and viewport state to the existing runtime
service boundary.

`Application` remains responsible for platform startup, logging setup, the main window, and the main loop. Runtime
modules should be initialized and shut down through `EngineRuntime`, so Player and Editor share the same service
lifecycle model. Service initialization failures inside `EngineRuntime` are unrecoverable startup failures: they should
be logged as fatal errors and terminate startup rather than returning partially initialized runtime state.
