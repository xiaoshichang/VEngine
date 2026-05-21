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

- Confirm the engine logging facade can route logs to console, file, and Editor console.

Prototype:

```text
VE_LOG_INFO
  -> Boost.Log core
  -> Console sink
  -> File sink
  -> Editor sink
```

## 3. Development Roadmap

### Milestone 0: Project Skeleton

- Create CMake root project.
- Create `CMakePresets.json`.
- Create `vcpkg.json`.
- Create `VEngine` static library target.
- Create `VEnginePlayer`, `VEngineEditor`, and `VEngineTests`.
- Integrate Boost and GoogleTest.
- Establish initial folder structure.
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

- Implement `LinearAllocator`.
- Implement `PoolAllocator`.
- Implement `FrameAllocator`.
- Implement math primitives.
- Implement Job System first version.
- Implement IO Thread.
- Add unit tests for Memory, Math, and Threading.

### Milestone 3: RHI Foundation

- Design common RHI.
- Implement D3D11RHI minimum path.
- Implement D3D12RHI minimum path.
- Add swapchain, buffer, texture, shader, pipeline, command list, and fence concepts.
- Add D3D11 and D3D12 triangle smoke tests.
- Build initial Render Thread and Render Command Queue.

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

- Implement GameObject and Component base model.
- Implement TransformComponent.
- Implement CameraComponent.
- Implement MeshRendererComponent.
- Implement LightComponent.
- Implement basic reflection registration.
- Implement Scene serialization.
- Implement simple ResourceManager.
- Render a static mesh with forward rendering.

### Milestone 6: Asset Pipeline

- Implement AssetDatabase.
- Define `.veasset`, `.vescene`, and `.vematerial`.
- Integrate assimp import.
- Import mesh source files.
- Track material and texture references.
- Create `VEngineAssetTool`.
- Add command line import path.
- Add Editor-triggered import path.

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
- Implement ScriptComponent.
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
- Implement AABB, Sphere, and Raycast.
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

## 4. Recommended Implementation Order

Recommended high-level order:

```text
CMake skeleton
  -> Core / Boost.Log / FileSystem
  -> Windows Platform
  -> Memory / Math / Threading
  -> RHI common design
  -> D3D11 and D3D12 minimum rendering
  -> Shader pipeline
  -> Scene / Resource
  -> Editor MVP
  -> C# scripting
  -> Runtime UI / lightweight physics
  -> Metal / iOS Simulator
```

D3D11, D3D12, and Metal should be considered together during RHI design. However, each backend should be implemented through small smoke-tested vertical slices instead of attempting a complete renderer immediately.

The most important first-stage deliverable is a working engine loop that can open a window, run a multithread-aware frame, render a static mesh, load a simple scene, and be inspected through the Editor.
