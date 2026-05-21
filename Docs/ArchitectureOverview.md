# VEngine Architecture Overview

## 1. Project Positioning

`VEngine` is a cross-platform lightweight 3D mini game engine. The first goal is not to build a full commercial engine, but to establish a clean, extensible runtime and editor architecture that can grow into a larger engine over time.

Primary goals:

- Provide a C++20 engine core managed by CMake.
- Build the engine core as a static library.
- Support Windows x64 and iOS.
- Provide a Windows Player, Windows Editor, and Windows unit tests.
- Provide an iOS Player that can run a demo in the iOS Simulator.
- Use a multithreaded architecture with Game Thread, Render Thread, Job System, and IO Thread.
- Support lightweight 3D rendering through D3D11, D3D12, and Metal.
- Use a traditional `GameObject + Component` scene model.
- Provide reflection, resource management, C# scripting, runtime UI, and an editor foundation.

Target platforms:

```text
Windows x64
  - Player
  - Editor
  - Unit Tests
  - D3D11 RHI
  - D3D12 RHI

iOS
  - Player
  - iOS Simulator demo in the first stage
  - Metal RHI
```

## 2. Non-Goals For The First Stage

The first stage should avoid over-expanding the scope. These systems are not first-stage goals:

- Full PBR rendering.
- Deferred rendering.
- Shadow system.
- Post-processing pipeline.
- Skeletal animation.
- Full rigid body physics.
- Terrain system.
- Material graph editor.
- Visual scripting.
- Complex prefab workflow.
- Runtime C# hot reload on iOS.
- Complete production-grade asset cooking pipeline.

The architecture should leave room for these features, but the first implementation should focus on a stable vertical slice.

## 3. Technical Constraints

The engine core uses `C++20`.

CMake is the project-level build system. The CMake project should expose all major build targets, platform options, backend options, and tool targets.

The engine allows independent third-party libraries, but avoids framework-style dependencies that take over the application loop, platform layer, window system, or engine architecture.

Allowed dependency style:

- Utility libraries.
- Importers.
- Logging backends.
- Test frameworks.
- Font libraries.
- Shader compilation tools.
- Editor immediate-mode UI library.

Avoided dependency style:

- Cross-platform application frameworks that own the main loop.
- Windowing frameworks that replace the custom platform layer.
- Full game frameworks that impose scene, asset, or runtime architecture.

Examples of acceptable first-stage dependencies:

```text
Boost
  - Boost.Log
  - Boost.JSON
  - Boost.UUID
  - Boost.Container
  - Boost.Lockfree
  - Boost.Stacktrace

assimp
  - Source model import

Dear ImGui
  - Windows Editor UI only

FreeType
  - Runtime UI font rasterization

GoogleTest
  - Windows unit tests

DirectXShaderCompiler
  - HLSL compilation
  - DXIL generation
  - SPIR-V generation

SPIRV-Cross
  - SPIR-V to MSL conversion
  - Shader reflection
```

Logging should be implemented on top of `Boost.Log`.

## 4. Third-Party Dependency Management

Dependency management should use a mixed approach:

- Windows: prefer `vcpkg` manifest mode for common third-party dependencies.
- Cross-platform or patched dependencies: wrap through `ThirdParty/` CMake files.
- Platform SDK dependencies: use platform-native SDKs directly.
- Avoid vendoring large source trees unless a dependency requires patches or stable pinning.

Recommended files:

```text
vcpkg.json
CMakePresets.json
ThirdParty/CMake/
ThirdParty/Patches/
```

The project should keep dependency policy explicit. Dependencies should be introduced only when they are independent enough and do not take ownership of the engine's architecture.

## 5. Build Targets

Recommended CMake targets:

```text
VEngine
  Static library containing the cross-platform engine core.

VEnginePlayer
  Windows player executable.

VEngineEditor
  Windows editor executable.

VEngineTests
  Windows unit test executable.

VEngineIOSPlayer
  iOS player app target.

VEngineAssetTool
  Command line asset import and cook tool.

VEngineShaderTool
  Command line shader compile, reflection, and cross-compile tool.
```

Windows first-stage outputs:

```text
VEngine.lib
VEnginePlayer.exe
VEngineEditor.exe
VEngineTests.exe
VEngineAssetTool.exe
VEngineShaderTool.exe
```

iOS first-stage outputs:

```text
VEngineIOSPlayer.app
```

## 6. Recommended Directory Layout

```text
VEngine/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json

  Engine/
    Runtime/
      Core/
      Platform/
      FileSystem/
      Logging/
      Memory/
      Threading/
      Math/
      Reflection/
      Scene/
      Resource/
      Render/
      Input/
      UI/
      Script/
      Physics/
    RHI/
      Common/
      D3D11/
      D3D12/
      Metal/
    Public/
    Private/

  Player/
    Windows/
    iOS/

  Editor/
    Windows/

  Tools/
    AssetTool/
    ShaderTool/

  Tests/
    Unit/
    Integration/

  Shaders/
    HLSL/
    Generated/
      MSL/
      DXIL/
      DXBC/
      Reflection/

  Assets/
    Samples/
    Engine/

  ThirdParty/
    CMake/
    Patches/

  Docs/
    ArchitectureOverview.md
    DevelopmentPlan.md
    BuildGuide.md
    CodingStyle.md
```

## 7. Module Architecture

### 7.1 Core

`Core` provides the lowest-level shared engine facilities:

- Basic type aliases.
- Assertions.
- Error codes.
- Time utilities.
- Application lifecycle interfaces.
- Build configuration.
- Platform feature macros.
- Non-copyable and non-movable utility base types.

`Core` should avoid depending on higher-level modules.

### 7.2 Platform

`Platform` isolates operating system differences.

Windows responsibilities:

- Win32 window creation.
- Win32 message loop.
- Keyboard and mouse input.
- High precision timer.
- Dynamic library loading.
- File path conversion.
- D3D11 and D3D12 surface / swapchain integration.
- Editor and Player window creation.

iOS responsibilities:

- Objective-C++ bridge.
- UIKit application lifecycle.
- View controller integration.
- `CAMetalLayer` or `MTKView` integration.
- Touch input.
- App pause / resume / termination handling.
- Metal drawable management.

The platform layer is self-owned by the engine. SDL, GLFW, Qt, and similar framework-style platform abstractions are not used.

### 7.3 FileSystem

`FileSystem` provides a unified file access layer:

- Path normalization.
- Virtual mount points.
- Synchronous file reads and writes.
- Asynchronous file requests.
- Read-only packaged resource access.
- Editor project directory access.
- Platform-specific user data paths.

Runtime should not directly use raw platform file APIs outside this module.

### 7.4 Logging

`Logging` is implemented with `Boost.Log`.

The engine should expose its own logging macros and logging facade:

```text
VE_LOG_TRACE
VE_LOG_DEBUG
VE_LOG_INFO
VE_LOG_WARN
VE_LOG_ERROR
VE_LOG_FATAL
```

Recommended sinks:

- Console sink.
- File sink.
- Editor console sink.
- Optional debugger output sink on Windows.

The public engine code should not depend directly on `Boost.Log` APIs everywhere. Most code should go through the VEngine logging facade so the backend remains replaceable if needed.

### 7.5 Memory

The first-stage memory system should implement the standard version:

- `LinearAllocator`.
- `PoolAllocator`.
- `FrameAllocator`.
- Tagged allocation.
- Basic allocation statistics.
- Leak detection in debug builds.

Allocator usage guidelines:

- Frame-local temporary data uses `FrameAllocator`.
- Fixed-size object pools use `PoolAllocator`.
- Sequential transient loading or import tasks use `LinearAllocator`.
- General engine objects may use default allocation first, then migrate to custom allocators once patterns are stable.

### 7.6 Threading

`Threading` contains:

- Thread wrapper.
- Thread naming.
- Mutex.
- Semaphore.
- Condition variable.
- Atomic utilities.
- Lock-free queues where appropriate.
- Job System.
- IO Thread.
- Thread fences and task counters.

Boost may be used for selected utilities such as lock-free queues, but the engine should keep its own threading facade.

### 7.7 Math

`Math` is self-owned and exposes VEngine math types:

- `Vector2`.
- `Vector3`.
- `Vector4`.
- `Matrix4`.
- `Quaternion`.
- `Transform`.
- `AABB`.
- `Sphere`.
- `Ray`.
- `Plane`.
- `Frustum`.

The public API should not expose DirectXMath or platform-native math types. Windows-specific SIMD optimization can be added internally later.

### 7.8 Reflection

`Reflection` supports editor inspection, serialization, component creation, and script binding.

First-stage capabilities:

- Type registration.
- Property registration.
- Component factory.
- Enum metadata.
- Default values.
- Serialization metadata.
- Editor visibility metadata.
- Runtime type lookup.

The first implementation can use explicit registration functions or macros. A metadata/code-generation tool can be introduced later.

### 7.9 Scene

`Scene` uses a traditional `GameObject + Component` model.

Core concepts:

- `Scene`.
- `GameObject`.
- `Component`.
- `TransformComponent`.

Component lifecycle:

```text
OnCreate
OnDestroy
OnEnable
OnDisable
OnUpdate
OnLateUpdate
```

`Scene` owns hierarchy, component storage, lifecycle dispatch, and serialization. Rendering, physics, UI, and script systems should observe or consume scene data through clear interfaces instead of freely coupling to scene internals.

### 7.10 Resource

`Resource` manages runtime resources and editor asset metadata.

Responsibilities:

- Asset GUIDs.
- Resource handles.
- Reference counting or lifetime tracking.
- Async loading.
- Hot reload notification.
- Dependency tracking.
- Runtime loading of engine-native resource formats.

Runtime should load processed engine assets rather than arbitrary source files. Editor and tools handle source import.

### 7.11 Render

`Render` is the high-level rendering layer above RHI.

Responsibilities:

- Render world management.
- Scene-to-render synchronization.
- Camera extraction.
- Light extraction.
- Mesh render queues.
- Material binding.
- Viewport rendering.
- Editor viewport support.

The render layer should avoid directly depending on GameObject instances on the Render Thread. It should consume render proxies, snapshots, or render commands produced by the Game Thread.

### 7.12 RHI

`RHI` is the graphics backend abstraction layer.

Backends:

- `D3D11RHI`.
- `D3D12RHI`.
- `MetalRHI`.

The common RHI should be designed closer to D3D12 and Metal than D3D11. D3D11 is implemented as a compatibility backend that internally maps modern concepts to D3D11's immediate/deferred context model.

### 7.13 Input

`Input` abstracts user input:

- Keyboard.
- Mouse.
- Touch.
- Future gamepad support.

Windows first stage:

- Keyboard.
- Mouse.

iOS first stage:

- Touch.

Input should be collected by the platform layer and consumed by the Game Thread through a stable input snapshot.

### 7.14 Runtime UI

Runtime UI is separate from Dear ImGui.

First-stage features:

- `Canvas`.
- `RectTransform`.
- `Image`.
- `Label`.
- `Button`.
- Basic layout.
- FreeType font rasterization.
- Mouse and touch event dispatch.
- Screen-space UI.

Dear ImGui is used only for Editor and debug UI, not as the game runtime UI system.

### 7.15 Script

`Script` hosts C# scripts on Windows.

First-stage Windows scope:

- Use `.NET` native hosting through `nethost` and `hostfxr`.
- Load a managed script assembly.
- Expose core native APIs through a C ABI bridge.
- Provide a handwritten `VEngine.ScriptAPI` layer in C#.
- Support `ScriptComponent`.
- Dispatch lifecycle methods such as `OnCreate`, `OnUpdate`, and `OnDestroy`.
- Support reloading after stopping the scene in Editor.

First-stage iOS scope:

- Do not run C# scripts.
- Run native demo logic.
- Keep the architecture open for future AOT-based investigation.

The iOS C# path should be treated as a separate research milestone because iOS has stricter runtime and dynamic-code constraints than Windows.

### 7.16 Physics

First-stage physics is intentionally lightweight.

Supported features:

- `AABB`.
- `Sphere`.
- `Raycast`.
- Basic `ColliderComponent`.
- Simple overlap tests.

Full rigid body simulation can be added later, either through a dedicated physics library or a custom lightweight physics layer.

## 8. Multithreaded Runtime Model

The engine uses the following conceptual threads:

```text
Main Thread
  Platform messages, app lifecycle, OS integration, editor event loop.

Game Thread
  Scene update, components, scripts, input consumption, gameplay state.

Render Thread
  Render command consumption, render world update, RHI command submission.

IO Thread
  File reads, raw asset data loading, async IO scheduling.

Worker Threads
  Job System workers for parallel tasks.

GPU Queue
  D3D12 / Metal explicit GPU queues, with D3D11 internally adapted.
```

The design should follow Unreal-style separation between Main Thread and Game Thread at the conceptual level. In the first implementation, Main Thread and Game Thread may run on the same physical thread to reduce complexity. The architecture should still avoid assuming they are always the same thread.

Important rules:

- Platform lifecycle events are owned by Main Thread.
- Scene mutation is owned by Game Thread.
- RHI command submission is owned by Render Thread.
- File reads are routed through FileSystem and IO Thread.
- Parallel jobs do not directly mutate GameObject hierarchy unless explicitly synchronized.
- Render Thread does not directly access live GameObject data.

Game Thread and Render Thread communicate through a double-buffered or triple-buffered render command queue.

Recommended frame flow:

```text
Main Thread
  Pump platform messages
  Tick application shell

Game Thread
  Consume input snapshot
  Update scripts
  Update components
  Update scene
  Build render commands / render snapshot

Render Thread
  Consume render commands
  Update render resources
  Build RHI command lists
  Submit to GPU queue

IO Thread + Worker Threads
  Process async loads
  Decode/import resource data
  Prepare upload requests
```

## 9. Job System

The Job System should start with a practical design:

```text
Job
JobHandle
JobQueue
WorkerThread
JobCounter
JobFence
ParallelFor
```

First-stage capabilities:

- Schedule fire-and-forget jobs.
- Schedule jobs with completion handles.
- Wait for a handle.
- Batch work through `ParallelFor`.
- Support dependency counters.
- Allow IO and Resource systems to schedule background processing.

Later capabilities:

- Task graph.
- Work stealing.
- Per-system queues.
- Fiber-based jobs.
- Profiling integration.

## 10. RHI Design

The RHI common layer should expose modern rendering concepts:

```text
Device
Queue
CommandAllocator
CommandList
Fence
Swapchain
Buffer
Texture
Sampler
Shader
PipelineState
DescriptorSet / BindGroup
RenderPass
Framebuffer
ResourceState
```

Backend intent:

- `D3D12RHI` maps naturally to the common RHI.
- `MetalRHI` maps naturally to the common RHI.
- `D3D11RHI` emulates missing explicit concepts internally.

First-stage rendering features:

- Static mesh.
- Camera.
- Transform.
- Material.
- Texture2D.
- Directional light.
- Forward rendering.
- Depth buffer.
- Basic shader pipeline.
- Swapchain rendering.
- Editor viewport rendering.

Not included in first-stage rendering:

- Deferred rendering.
- Full PBR.
- Shadow maps.
- Compute pipeline.
- Skeletal animation.
- Post-processing stack.

## 11. Shader Pipeline

The engine uses HLSL as the source shader language.

Recommended shader flow:

```text
HLSL Source
  -> DirectXShaderCompiler
    -> DXIL for D3D12
    -> DXBC-compatible output or fallback path for D3D11
    -> SPIR-V
      -> SPIRV-Cross
        -> MSL for Metal
        -> Reflection metadata
```

`VEngineShaderTool` responsibilities:

- Compile HLSL.
- Generate DXIL for D3D12.
- Support D3D11 shader output path.
- Generate SPIR-V.
- Convert SPIR-V to MSL.
- Export shader reflection metadata.
- Report compile errors in a format consumable by Editor.
- Generate shader variants when needed.

Runtime should load compiled shader artifacts and reflection metadata. The Player should not rely on runtime shader cross-compilation for normal execution.

Recommended generated layout:

```text
Shaders/
  HLSL/
  Generated/
    DXIL/
    DXBC/
    MSL/
    Reflection/
```

## 12. Resource And Asset Pipeline

Editor and tools are responsible for importing source assets. Runtime is responsible for loading VEngine-native assets.

Recommended asset pipeline:

```text
Source Asset
  -> AssetImporter
  -> Engine Asset Metadata
  -> Runtime Resource
  -> ResourceManager
```

`assimp` is used for importing common model formats:

- `fbx`.
- `obj`.
- `gltf`.
- `glb`.

Resource files should be as text-friendly as practical:

```text
.veasset
  Text metadata: GUID, source path, import settings, dependencies.

.vescene
  Text scene file: hierarchy, components, serialized properties.

.vematerial
  Text material file: shader reference, parameters, textures.

.veprefab
  Text prefab file: serialized GameObject subtree.

.vemesh
  Prefer text in the earliest stage for debugging.
  Later add binary cooked cache for large mesh data.

.vetex
  Prefer text metadata in the earliest stage.
  Later add platform texture cache for performance.
```

Because mesh and texture payloads can become large, the project should eventually add cooked binary caches while keeping human-readable metadata.

## 13. GameObject, Component, And Reflection

The engine uses a traditional object/component model:

```text
Scene
  GameObject
    TransformComponent
    Component...
```

Recommended built-in first-stage components:

- `TransformComponent`.
- `CameraComponent`.
- `MeshRendererComponent`.
- `LightComponent`.
- `ScriptComponent`.
- `CanvasComponent`.
- `ColliderComponent`.

Reflection should support:

- Component discovery.
- Component creation by type.
- Property editing in Inspector.
- Scene serialization.
- Prefab serialization.
- Script binding metadata.

Reflection registration can be explicit in the first stage. Code generation can be added later if manual registration becomes too costly.

## 14. C# Scripting

Windows scripting uses `.NET` native hosting.

Recommended native hosting route:

```text
C++ Host
  -> nethost locates hostfxr
  -> hostfxr initializes .NET runtime
  -> load managed assembly
  -> obtain managed entry points / delegates
  -> dispatch script lifecycle
```

Native-to-managed boundary:

```text
C++ engine
  -> C ABI bridge
  -> VEngine.ScriptAPI C# wrapper
  -> User script assembly
```

First-stage API binding approach:

- Handwrite core APIs.
- Avoid automatic binding generation initially.
- Keep API shape stable and small.
- Expose only necessary GameObject, Component, Transform, Input, Time, and Logging APIs.

Script lifecycle:

```text
OnCreate
OnDestroy
OnEnable
OnDisable
OnUpdate
```

Editor reload policy:

- Basic implementation target: stop scene, unload/recreate script context, reload assembly.
- Standard architecture target: allow Editor to rebuild C# project and reload script domain while preserving serialized scene data.
- Advanced runtime state-preserving hot reload is not a first-stage requirement.

iOS first-stage policy:

- C# scripting is disabled.
- Demo logic is native.
- Future support requires a separate AOT and runtime feasibility milestone.

## 15. Editor Architecture

The Windows Editor uses Dear ImGui.

Editor first-stage panels:

```text
Main Menu
Scene Hierarchy
Inspector
Asset Browser
Viewport
Console / Log
Project Settings
Import Settings
Play / Stop Toolbar
```

Editor principles:

- ImGui is only for Editor and debug UI.
- Runtime UI remains separate.
- Editor edits scene through public Scene and Reflection APIs.
- Editor imports assets through AssetDatabase and AssetImporter.
- Editor Viewport renders through the engine Render and RHI layers.
- Editor Console receives logs through the VEngine `Boost.Log` facade.

First-stage Editor workflow:

```text
Open project
  -> Scan assets
  -> Open scene
  -> Edit hierarchy and properties
  -> Import assets
  -> Press Play
  -> Stop and return to edit mode
```

## 16. Runtime UI

Runtime UI is a game-facing UI system.

First-stage features:

- Screen-space Canvas.
- Rect transform hierarchy.
- Image rendering.
- Label rendering with FreeType.
- Button interaction.
- Simple layout.
- Mouse input on Windows.
- Touch input on iOS.

The runtime UI renderer should integrate with the Render system and RHI, not with ImGui.

## 17. Platform Architecture

### 17.1 Windows

Windows platform layer:

- Owns Win32 window creation.
- Owns Win32 message loop.
- Produces input events.
- Creates D3D-compatible native window handles.
- Supports Player and Editor shells.
- Supports dynamic library loading for scripting.
- Provides platform file path utilities.

### 17.2 iOS

iOS platform layer:

- Uses Objective-C++ for native bridge files.
- Owns UIKit lifecycle integration.
- Creates an iOS view backed by `CAMetalLayer` or `MTKView`.
- Forwards touch events to Input.
- Coordinates pause and resume.
- Provides Metal drawable and surface access to `MetalRHI`.

First-stage iOS target is iOS Simulator demo execution.

## 18. Testing Strategy

Windows unit tests use GoogleTest.

Unit test areas:

```text
Core
Math
Memory
Threading
Reflection
Scene serialization
Resource metadata
FileSystem path behavior
```

Integration test areas:

```text
Asset import smoke test
Shader compile smoke test
D3D11 RHI smoke test
D3D12 RHI smoke test
C# script host smoke test
```

Manual/sample tests:

```text
Windows Player demo
Windows Editor demo
iOS Simulator demo
```

RHI smoke tests should start simple:

- Create device.
- Create swapchain.
- Create buffer.
- Create texture.
- Compile shader.
- Draw triangle.

## 19. CMake Build Plan

Recommended options:

```cmake
option(VE_BUILD_PLAYER "Build Windows player" ON)
option(VE_BUILD_EDITOR "Build Windows editor" ON)
option(VE_BUILD_TESTS "Build tests" ON)
option(VE_BUILD_TOOLS "Build tools" ON)
option(VE_BUILD_IOS_PLAYER "Build iOS player" OFF)

option(VE_ENABLE_D3D11 "Enable D3D11 RHI" ON)
option(VE_ENABLE_D3D12 "Enable D3D12 RHI" ON)
option(VE_ENABLE_METAL "Enable Metal RHI" OFF)
option(VE_ENABLE_SCRIPTING "Enable C# scripting" ON)
```

Recommended presets:

```text
windows-msvc-debug
windows-msvc-release
windows-msvc-editor
windows-msvc-tests
ios-simulator-debug
ios-simulator-release
```

iOS may be generated through CMake/Xcode, while keeping a small amount of iOS-specific template content such as `Info.plist`, app delegate, view controller, and entitlement files.

## 20. Development Plan

The development roadmap, early technical validation items, and recommended implementation order are maintained separately in `Docs/DevelopmentPlan.md`.
