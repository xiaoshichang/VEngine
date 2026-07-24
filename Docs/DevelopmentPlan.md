# VEngine Development Plan

## 1. Purpose

This document describes the rough development plan for `VEngine`.

The plan is intentionally milestone-oriented. Each milestone should produce a usable vertical slice instead of only isolated low-level systems. The goal is to reduce architectural risk early, especially around the RHI design, shader cross-compilation, C# hosting, and macOS Metal integration.

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
  -> FXC
    -> DXBC
  -> Slang
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

### 2.4 macOS Metal Flow

Goal:

- Confirm CMake/Xcode target can run a native macOS app using Metal.

Prototype:

```text
macOS app
  -> AppKit lifecycle
  -> Metal device
  -> Draw triangle
```

### 2.5 Boost.Log Integration

Goal:

- Confirm the engine logging facade can route logs to console, file, and the Editor Console callback path.

Prototype:

```text
VE_LOG_INFO
  -> Boost.Log core
  -> Console sink
  -> File sink
  -> Editor callback sink
```

## 3. Development Roadmap

Current status: Milestones 0-2 are broadly implemented. Milestones 3-7 are partially implemented through the common
RHI, D3D11/D3D12/Metal backend slices, RenderSystem, ShaderTool, Scene/Resource systems, Editor asset database, Editor
viewport/panels, Windows packaging flow, and a first macOS packaging path. Mac targets currently use early player/editor
shells while the full macOS platform backend remains future work; the macOS player is built as a normal executable and
the macOS Editor packages it into a generated `.app` with app-local .NET JIT scripting payloads. The iOS track now has
an initial `VEngineIOSPlayer` target, iOS CMake presets, UIKit/Metal app shell, a NativeAOT-oriented scripting backend
boundary with weak-symbol bridge discovery, and a first macOS Editor iOS packer that publishes NativeAOT project scripts,
exports a type-filtered package data manifest, configures Xcode, archives, and exports through `xcodebuild`. Runtime scene loading, touch input,
device verification, and production signing UX remain future work.

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
- Add swapchain, buffer, texture, shader, pipeline, command list, and fence concepts.
- Build initial RenderSystem with Render Thread and lock-free Render Command Queue.
- Connect RenderSystem lifecycle through `EngineRuntime`.
- Connect RHI device and swapchain lifecycle through RenderSystem after the service boundary is stable.

### Milestone 4: Shader Pipeline

- Integrate DirectXShaderCompiler.
- Compile HLSL to DXIL.
- Compile HLSL to DXBC for D3D11.
- Generate MSL through Slang.
- Generate shader reflection metadata.
- Create `VEngineShaderTool`.
- Add shader compile smoke tests.

### Milestone 5: Scene And Rendering Vertical Slice

- Implement GameObject and Component base model.
- Implement TransformComponent.
- Implement CameraComponent.
- Implement MeshRenderComponent.
- Implement LightComponent.
- Implement SceneSystem with active Scene ownership and Scene Thread update.
- Implement basic reflection registration.
- Implement Scene serialization.
- Implement simple ResourceManager.
- Render a static mesh with forward rendering.
- Connect Scene, Resource, and Render systems through the `EngineRuntime` runtime context.

### Milestone 6: Asset Pipeline

- Implement AssetDatabase.
- Define `.veasset`, `.vescene`, and `.vematerial`.
- Integrate assimp import.
- Import mesh source files.
- Track material and texture references.
- Create `VEngineAssetTool`.
- Add command line import path.
- Add Editor-triggered import path.
- Route asset import and runtime loading work through Resource, IOSystem, and Job System services owned by
  `EngineRuntime`.

### Milestone 7: Editor MVP

- Integrate Dear ImGui.
- Add docking-based Editor shell.
- Add main menu.
- Add Scene Hierarchy.
- Add Inspector.
- Add Asset Browser.
- Add Viewport.
- Add Console / Log panel.
- Add Play / Stop.
- Edit component properties through Reflection.

### Milestone 8: C# Scripting Windows MVP

- Integrate `nethost` and `hostfxr`.
- Load managed script assembly.
- Add C ABI native bridge.
- Handwrite core `VEngine.ScriptAPI`.
- Implement the `ScriptableComponent` interface and first `DotnetScriptableComponent`.
- Dispatch `OnCreate`, `OnUpdate`, and `OnDestroy`.
- Support reload after stopping scene in Editor.
- Add script host smoke test.

### Milestone 9: Runtime UI And Lightweight Physics

- Implement Canvas.
- Implement RectTransform.
- Implement Image.
- Implement Label with FreeType.
- Implement Button.
- Implement UI event routing.
- Implement AABB, Sphere, and Raycast for picking and lightweight physics.
- Implement ColliderComponent and RigidbodyComponent.
- Add explicit Scene-to-PhysicsSystem synchronization APIs before runtime frame-loop integration.
- Add simple UI and picking demo.

### Milestone 10: macOS Metal Demo

- Create macOS Player target.
- Add Objective-C++ platform bridge.
- Add AppKit app lifecycle.
- Add Metal surface integration.
- Implement MetalRHI minimum path.
- Use HLSL to MSL shader flow.
- Draw simple 3D demo in macOS.
- Add input demo.

### Milestone 11: iOS Player And Packaging

- Add iOS CMake/Xcode presets.
- Add `VEngineIOSPlayer` as a UIKit app bundle target.
- Split iOS and macOS platform macros, target sources, and framework links.
- Add a UIKit-backed Metal surface and route it through the shared RenderSystem path.
- Add touch input and lifecycle pause/resume handling.
- Load packaged runtime assets from the iOS app bundle.
- Use .NET NativeAOT for iOS scripting instead of the desktop JIT host.
- Add an Editor iOS packer that drives Xcode archive/export and produces a signed `.ipa`.
- Add iOS third-party dependency setup for runtime libraries that need platform-specific static builds.

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
  -> Metal / macOS
```

D3D11, D3D12, and Metal should be considered together during RHI design. However, each backend should be implemented through small smoke-tested vertical slices instead of attempting a complete renderer immediately.

### GPU Driven Virtual Shadow Maps

The directional-light VSM path now has a GPU-driven Windows vertical slice:

- D3D11/D3D12 compute pipelines and structured SRV/UAV buffers in the common RHI.
- FrameGraph compute passes and versioned imported buffers.
- Current-frame receiver depth prepass and per-pixel logical-page marking.
- Center-only receiver marking followed by logical-page request expansion and compaction, avoiding redundant per-pixel 3-by-3 atomics.
- Persistent per-view physical-page metadata with GPU cache lookup and LRU replacement.
- Per-level parallel resident-page hit resolution interleaved with serial miss allocation in coarse-to-fine order, with early termination when the physical pool is fully pinned.
- Bounds-local dynamic-caster invalidation: moving, added, removed, or shadow-state-changing casters dirty only resident pages covered by their old/new bounds while preserving compatible page mappings.
- Page-instanced clear/caster rendering without CPU page-request readback.
- Dense page-table sampling in opaque and transparent forward passes.
- GPU-driven VSM is the only implementation. D3D11 and D3D12 run the GPU path; Metal currently has no VSM. GPU resource or pipeline failure disables VSM for the affected view while preserving scene rendering and a valid disabled-shadow sampling binding.

GPU cache reset requests are consumed independently from resource-recreation resets. Dynamic caster revisions no longer trigger a whole-pool reset: the CPU produces a compact list of absolute XY page identities and clipmap levels from changed caster bounds, and the GPU marks only matching persistent physical pages dirty. These identities are not clipped to the current camera working region and ignore the depth epoch during invalidation matching, so dormant resident pages cannot retain stale shadows after the camera moves away and later returns. Disabled or invalid frames do not consume compatibility or caster history before a GPU clear pass can receive it. Full mapping resets remain reserved for incompatible changes such as a directional-light basis change, shadow-distance change, device/resource recreation, or explicit cache reset. Overflow conservatively dirties all resident content without discarding mappings.

The Editor Scene View uses a 2,560-pixel atlas (400 physical pages) so its maximized viewport can retain complete coarse fallback coverage without the 4,096-pixel Game View footprint. In the maximized 2576-by-1416 dual-view D3D12 Editor profiling scene, the corrected cache lifetime, request compaction, and per-level scheduling restored the static frame rate from about 20–25 FPS to 75.0 FPS. Bounds-local invalidation then removed the Play-mode regression: with all physics objects moving, the same scene remained at 75.0 FPS instead of falling back to about 25 FPS.

Native Metal compute encoding, GPU diagnostics/counters, accelerated large invalidation-set lookup, and a general indirect GPU-scene caster path remain future rendering milestones.

The most important first-stage deliverable is a working engine loop that can open a window, run a multithread-aware frame, render a static mesh, load a simple scene, and be inspected through the Editor.

## 5. EngineRuntime Integration

`EngineRuntime` is the shared runtime service layer used by Player, Editor, tools, and future platform backends. It owns
the lifecycle of long-lived engine services and provides explicit access to them without requiring global singletons.

Current services:

- Job System.
- IOSystem.
- InputSystem.
- TimeSystem.
- SceneSystem.
- RenderSystem.
- ResourceSystem.

Later services should connect through this layer as their modules land:

- Runtime UI.

Already connected through this layer:

- Scripting.
- PhysicsSystem runtime frame-loop integration.

RenderSystem now owns the Render Thread and the RHI device/swapchain lifecycle through this service boundary. Future
rendering work should continue to attach render-resource and viewport state through the same model.

`Application` remains responsible for platform startup, logging setup, the main window, and the main loop. Runtime
modules should be initialized and shut down through `EngineRuntime`, so Player and Editor share the same service
lifecycle model. Service initialization failures inside `EngineRuntime` are unrecoverable startup failures: they should
be logged as fatal errors and terminate startup rather than returning partially initialized runtime state.
