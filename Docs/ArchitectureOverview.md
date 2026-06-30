# VEngine Architecture Overview

## 1. Project Positioning

`VEngine` is a cross-platform lightweight 3D mini game engine. The first goal is not to build a full commercial engine, but to establish a clean, extensible runtime and editor architecture that can grow into a larger engine over time.

Primary goals:

- Provide a C++20 engine core managed by CMake.
- Build the engine core as a static library.
- Support Windows x64 and macOS.
- Provide a Windows Player, Windows Editor, and Windows unit tests.
- Provide a macOS Player and macOS Editor placeholder while the platform backend is still being established.
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

macOS
  - Player
  - Editor placeholder
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
- Runtime C# hot reload on macOS remains a future milestone.
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

Jolt Physics
  - Future runtime physics backend
  - Rigid body simulation and collision detection

DirectXShaderCompiler
  - HLSL compilation
  - DXIL generation

Slang
  - HLSL to Metal MSL generation
  - Shader reflection
```

Logging should be implemented on top of `Boost.Log`.

Windows tests should be registered through CMake/CTest. The first stage does not introduce a third-party C++ test
framework.

## 4. Third-Party Dependency Management

Dependency management should use a repository-owned `ThirdParty/` approach:

- Third-party source archives, source checkouts, prebuilt tool packages, and built libraries live under `ThirdParty/`.
- CMake wrapper files under `CMake/` locate, download, or build those dependencies.
- A platform build script such as `ThirdParty/Build_Windows64.bat` prepares dependencies after clone.
- The first Windows dependency and build baseline is Visual Studio 2022 Build Tools, or Visual Studio 2022, with MSVC
  v143 x64/x86 build tools. The `windows-msvc-*` presets use the `Visual Studio 17 2022` generator and explicitly request
  `v143`; newer Visual Studio installations are not a replacement unless a new preset and dependency baseline are
  introduced.
- Generated dependency payloads should be ignored by git unless the project explicitly decides to vendor a small file.
- Platform SDK dependencies: use platform-native SDKs directly.
- Avoid committing large source trees or binary packages unless a dependency requires patches or stable pinning.

Recommended files:

```text
CMakePresets.json
CMake/Setup<Dependency>.cmake
ThirdParty/Setup_<Platform>.bat
ThirdParty/<Dependency>/.gitignore
ThirdParty/<Dependency>/README.md
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

VEngineMacPlayer
  macOS player app target. The current implementation is an early AppKit shell that initializes the shared runtime path
  but does not yet provide a complete macOS window/application backend.

VEngineAssetTool
  Command line asset import and cook tool target. The current executable is a lightweight placeholder while the Editor
  owns the implemented project asset database and package flow.

VEngineShaderTool
  Command line shader compile, reflection, and cross-compile tool.
```

Windows first-stage outputs:

```text
VEngine.lib
VEnginePlayer.exe
VEngineEditor.exe
VEngineAssetTool.exe
VEngineShaderTool.exe
```

Windows unit and smoke test executables are registered through CTest. Individual test target names are intentionally kept
out of this high-level target list so new module tests can be added without changing the architecture overview.

Planned macOS first-stage outputs:

```text
VEngineMacPlayer.app
VEngineMacEditor.app
```

The cross-platform `Application` window backend is still Windows-only because `Window::Create()` only has a Win32
implementation. The repository now includes macOS player and editor placeholder targets; a complete macOS
application/window backend remains future work.

## 6. Recommended Directory Layout

This is the target repository shape. Some directories and documents are introduced only when their owning milestone
starts, so this layout is not expected to exactly match the current working tree at every point in development.

```text
VEngine/
  CMakeLists.txt
  CMakePresets.json

  CMake/
    Scripts/
      WithMsvc.bat
    Toolchains/
      Windows64.cmake
      Mac.cmake

  Engine/
    Runtime/
      Core/
      Platform/
      FileSystem/
      Logging/
      Memory/
      Threading/
      Jobs/
      IO/
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
    macOS/

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
- Error and result utilities.
- Source location utilities.
- Build configuration.
- Platform feature macros.
- Small standalone utility types such as non-copyable, scope-exit, and enum flag helpers.

`Core` should avoid depending on higher-level modules.

Detailed Core boundaries, first-stage APIs, and implementation scope are defined in `Docs/CoreModuleDesign.md`.

### 7.2 Platform

`Platform` isolates operating system differences.

Current Windows first-stage implementation:

- Win32 window creation.
- Win32 message loop.
- Native window handle access for early RHI and demo integration.
- Debug console integration for log output and GM command input.
- Keyboard and mouse OS event collection.
- Empty Player and Editor shell window creation.

Future Windows responsibilities:

- High precision timer.
- Dynamic library loading.
- File path conversion.
- D3D11 and D3D12 surface / swapchain integration.

Milestone 10 macOS responsibilities:

- Objective-C++ bridge.
- AppKit application lifecycle.
- View controller integration.
- `CAMetalLayer` or `MTKView` integration.
- Keyboard and mouse input.
- App activation / deactivation / termination handling.
- Metal drawable management.

The current `Application::Run()` backend is Windows-only. macOS application lifecycle support should land with the macOS
Simulator milestone instead of being implied by the Milestone 1 platform work.

The platform layer is self-owned by the engine. SDL, GLFW, Qt, and similar framework-style platform abstractions are not used.

### 7.3 Application And EngineRuntime

`Application` owns platform startup, logging setup, the main window, and the main loop. Player and Editor should share
this application-level flow instead of duplicating service initialization in each executable entry point.

`EngineRuntime` owns long-lived runtime services used by Player, Editor, tools, and future platform backends. It provides
explicit service access without introducing global singletons. The current runtime services are JobSystem, IOSystem,
InputSystem, TimeSystem, SceneSystem, RenderSystem, and ResourceSystem; Script, UI, and Physics should connect through
this layer as their modules land.

### 7.4 FileSystem

`FileSystem` provides a unified file access layer:

- Path normalization.
- Synchronous file reads and writes.
- Basic directory creation, removal, queries, and non-recursive listing.
- Explicit project root resolution.
- UTF-8 engine-facing paths with UTF-16 conversion at Windows API boundaries.

Runtime should not directly use raw platform file APIs outside this module.

Detailed first-stage FileSystem behavior is defined in `Docs/FileSystemDesign.md`. Virtual mount points, asynchronous file
requests, read-only packaged resource access, and platform-specific user data paths are future layers over the same
path conventions.

### 7.5 Logging

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
- Editor console callback sink.
- Optional debugger output sink on Windows.

The public engine code should not depend directly on `Boost.Log` APIs everywhere. Most code should go through the VEngine logging facade so the backend remains replaceable if needed.

Detailed Logging facade APIs, initialization rules, sinks, formatting, assertion integration, and tests are defined in `Docs/LoggingFacadeDesign.md`.

### 7.6 Memory

The first-stage memory system should stay small and implement only allocator work needed by current modules:

- `PoolAllocator`.
- Per-pool allocation statistics exposed by `PoolAllocator`.

Deferred allocator work:

- `LinearAllocator` is not a first-stage Milestone 2 deliverable.
- `FrameAllocator` is not a first-stage Milestone 2 deliverable.
- Tagged allocation is not a first-stage Milestone 2 deliverable.
- Engine-wide allocation statistics are not a first-stage Milestone 2 deliverable.
- Leak detection in debug builds is not a first-stage Milestone 2 deliverable.
- Add deferred allocators later when loading, import, frame-local, or render-command lifetimes are clear enough to justify
  them.

Allocator usage guidelines:

- Fixed-size object pools use `PoolAllocator`.
- General engine objects may use default allocation first, then migrate to custom allocators once patterns are stable.
- Future frame-local temporary data may use `FrameAllocator` after the frame lifetime model is stable.
- Future sequential transient loading or import tasks may use `LinearAllocator` after those pipelines are stable.

### 7.7 Threading

`Threading` contains low-level threading building blocks:

- Cross-platform thread wrapper.
- Thread naming.
- Mutex.
- Semaphore.
- Condition variable.
- Atomic utilities.
- Lock-free queues where appropriate.

Boost may be used for selected utilities such as lock-free queues, but the engine should keep its own threading facade.

Detailed first-stage thread wrapper, synchronization primitive, and lock-free utility rules are defined in
`Docs/ThreadingDesign.md`.

`JobSystem` and `IOSystem` are higher-level runtime systems built on the Threading and FileSystem foundations. They are
owned by `EngineRuntime` rather than by the low-level Threading module.

Thread fences, task counters, and other cross-system coordination primitives should be added later when Render,
Resource, and Scene lifetimes expose stable requirements.

### 7.8 Math

`Math` is self-owned and exposes VEngine math types:

- `Vector2`.
- `Vector3`.
- `Vector4`.
- `Matrix44`.
- `Quaternion`.

Geometry math types should be added with the systems that first need them:

- `AABB`.
- `Sphere`.
- `Ray`.
- `Plane`.
- `Frustum`.

The public API should not expose DirectXMath or platform-native math types. Windows-specific SIMD optimization can be added internally later.

### 7.9 Reflection

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

### 7.10 Scene

`Scene` uses a traditional `GameObject + Component` model.

Core concepts:

- `Scene`.
- `GameObject`.
- `Component`.
- `TransformComponent`.
- `MeshRenderComponent`.
- `CameraComponent`.
- `LightComponent`.

`SceneSystem` owns the active `Scene` and starts the Scene Thread that updates the scene hierarchy. Main Thread and Game
Thread remain conceptually distinct; this first implementation gives scene update a dedicated runtime service boundary
without making Render Thread code read live `GameObject` state.

Component lifecycle:

```text
OnCreate
OnDestroy
OnEnable
OnDisable
OnUpdate
OnLateUpdate
```

`Scene` owns root `GameObject` storage, lifecycle dispatch, and serialization. Parent-child tree ownership is maintained by
`TransformComponent`, while `GameObject` keeps a fixed first-stage component slot layout (`Transform`, `MeshRender`,
`Camera`, `Light`) rather than an open-ended component container. `GameObject` does not carry standalone parent, child, or
active state. Rendering, physics, UI, and script systems should observe or consume scene data through clear interfaces
instead of freely coupling to scene internals.

### 7.11 Resource

`Resource` owns loaded resource objects and low-level runtime loading primitives. It does not own editor project scans,
asset import policy, or player manifest export policy.

Responsibilities:

- Loading engine-native resource payloads from explicit platform paths.
- Deserializing resource files into CPU-side resource data.
- Creating concrete `ResourceObject` instances such as `SceneResource`, `MeshResource`, `MaterialResource`, and
  `TextureResource`.
- Storing the `AssetRecord` on each loaded `ResourceObject` so dependencies remain visible to resource lifetime logic.
- Resource object caching, request/release reference counting, and root-reachability collection.
- Preserving render-thread resource creation hooks through the Render command queue when a resource needs GPU state.

Asset indexing and manifest ownership live above the loaded-resource layer:

- `AssetID` identifies a loadable asset or sub-asset through `GUID + subID`; first-stage assets use `subID = 0`.
- `EditorAssetDatabase` owns editor asset records, project-relative asset path to `AssetID` lookup, `AssetID` to
  asset-record lookup, import artifacts, dependency records, and live editor updates while a project is open.
- `RuntimeAssetLoader` owns the read-only player asset manifest exported during packaging, packaged runtime path to
  `AssetID` lookup, and internal `AssetID` to manifest-record lookup.
- Both editor and runtime record sources provide common `AssetRecord` values through `IAssetRecordProvider`.

Editor and runtime asset indexing are intentionally separated. `EditorResourceLoader` is only an editor convenience
wrapper around `ResourceSystem::Request()`, while `RuntimeAssetLoader` resolves packaged manifest asset records.
Runtime should load processed engine assets rather than arbitrary source files. Editor and tools handle source import.

Components store resource references as `AssetRef<TResource>`. `AssetRef` serializes only its `AssetID`; after a
successful `ResourceSystem::Request<TResource>()`, it owns that requested reference and holds the loaded
`ResourceObject` pointer for CPU-side or render-facing access. `AssetRef` is movable but not copyable; resetting,
reassigning, or destroying it releases its current requested reference through `ResourceSystem`.

`ResourceSystem::Request<TResource>()` is a CPU-side load and lifetime operation. Render-facing state is initialized
explicitly through `ResourceSystem::EnsureRenderResource(assetRef, renderSystem)` when an `AssetRef` enters a render
path, such as during Scene construction for mesh and material components. `ResourceSystem` initializes dependency render
resources before the requested resource, tracks whether init commands have been queued, and releases a resource's render
state before releasing its dependency references when the CPU reference count reaches zero.

Editor resource lifetime follows root-reachability collection using active root `AssetID` values from the active scene,
selection state, previews, thumbnails, and importer views. Player resource lifetime is normally expressed by owning
`AssetRef` handles returned from `ResourceSystem::Request()`. Release is driven by `AssetRef` lifetime; `ResourceSystem`
keeps internal release helpers for request rollback, type mismatch cleanup, dependency release, and `AssetRef`
reset/destruction. `ResourceSystem::CollectUnusedResources()` never unloads resources with nonzero reference counts.

### 7.12 Render

`Render` is the high-level rendering layer above RHI.

Responsibilities:

- Render world management.
- Scene-to-render synchronization.
- Camera extraction.
- Light extraction.
- Mesh render queues.
- Material binding.
- Viewport rendering.
- ViewportClient ownership and viewport binding.
- RenderTarget description for window and texture-backed outputs.
- Editor viewport support.

Viewport clients model one active binding at a time:

- Window surface binding for Player or on-screen editor panes.
- Off-screen render target binding for scene previews, thumbnails, and embedded game views.

The viewport client keeps these modes mutually exclusive so camera data can stay separate and be merged later when scene
render work is assembled for the Render Thread.

Render-facing resource ownership follows an Unreal-style split between the Scene Thread and Render Thread:

- Scene Thread owns CPU-side render objects such as `RenderTarget`, `RenderTexture`, future mesh render data, material
  state, texture resources, and viewport state.
- Render Thread owns matching render proxies whose type names use an `RT` prefix, such as `RTRenderTexture`.
- CPU-side render objects may keep a `std::shared_ptr` to their RT proxy so the Render Thread can finish already
  queued work after the Scene Thread object is destroyed.
- Objects with RT proxies must not call RHI directly from the Scene Thread. They initialize or update render-side state
  through an `InitRenderResource`-style API that submits work through the `RenderSystem` render command queue.
- RT proxy objects are the only side that creates, mutates, or destroys live RHI resources.

Example:

```text
Scene Thread:
  RenderTarget
    Describes kind, extent, format, and logical output ownership.

  RenderTexture
    Bundles a texture-backed RenderTarget description with render-resource-view access.
    Holds shared_ptr<RTRenderTexture>.
    Calls InitRenderResource(RenderSystem&) when RHI-backed state is needed.

Render Thread:
  RTRenderTexture
    Owns RHI texture, render target view, and sampled/resource view references.
    Receives initialization, resize, and release commands.
```

This model lets Scene Thread code keep ordinary CPU descriptions while Render Thread code owns the backend-specific
objects and timing-sensitive destruction. `RenderTexture` can be destroyed on the Scene Thread while `RTRenderTexture`
survives until the last render command that captured it has completed.

The render layer should avoid directly depending on live `GameObject` instances on the Render Thread. It should consume
render proxies, snapshots, or render commands produced by the Scene Thread.

`RenderSystem` owns Render Thread lifecycle and the render command queue. Detailed first-stage service, thread, and
command queue rules are defined in `Docs/RenderSystemDesign.md`. RHI documents remain focused on backend graphics
objects such as devices, swapchains, resources, command lists, pipelines, bindings, and GPU synchronization.

### 7.13 RHI

`RHI` is the graphics backend abstraction layer.

Backends:

- `D3D11RHI`.
- `D3D12RHI`.
- `MetalRHI`.

The common RHI should be designed closer to D3D12 and Metal than D3D11. D3D11 is implemented as a compatibility backend that internally maps modern concepts to D3D11's immediate/deferred context model.

### 7.14 Input

`Input` abstracts user input:

- Keyboard.
- Mouse.
- Touch.
- Future gamepad support.

Windows first stage:

- Keyboard.
- Mouse.

macOS first stage:

- Touch.

Input should be collected by the platform layer and consumed by the Game Thread through a stable input snapshot.
The first implementation routes Win32 keyboard and mouse messages through `OSEventQueue`; `SceneSystem` consumes those
events on the Scene Thread and updates `InputSystem` before scene/editor frame work runs.
When an Editor callback is installed, Editor receives OS input events first and explicitly returns whether an event should
continue into `InputSystem`. Without an Editor callback, input events flow directly to `InputSystem`.

### 7.15 Runtime UI

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

### 7.16 Script

`Script` hosts C# scripts on Windows.

First-stage Windows scope:

- Use `.NET` native hosting through `nethost` and `hostfxr`.
- Load a managed script assembly.
- Expose core native APIs through a C ABI bridge.
- Provide a handwritten `VEngine.ScriptAPI` layer in C#.
- Support a `ScriptableComponent` interface with `DotnetScriptableComponent` as the first concrete script component.
- Dispatch lifecycle methods such as `OnCreate`, `OnDestroy`, `OnUpdate`, `OnLateUpdate`, `OnEnable`, and `OnDisable`.
- Support reloading after stopping the scene in Editor.

First-stage macOS scope:

- Do not run C# scripts.
- Run native demo logic.
- Keep the architecture open for future AOT-based investigation.

The macOS C# path should be treated as a separate research milestone because Apple platform runtime constraints still shape the implementation.

### 7.17 Physics

First-stage physics is intentionally lightweight.

Supported features:

- `AABB`.
- `Sphere`.
- `Raycast`.
- Basic `ColliderComponent`.
- Simple overlap tests.

Full rigid body simulation can be added later, either through a dedicated physics library or a custom lightweight physics layer.
Jolt Physics is prepared as the first project-owned third-party candidate for that later full rigid body backend, but
the engine-facing Physics facade, scene synchronization, and component model remain separate future work.

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
- Scene mutation and CPU-side render resource mutation are owned by Game Thread / Scene Thread.
- RHI command submission is owned by Render Thread.
- File reads are routed through FileSystem and IO Thread.
- Parallel jobs do not directly mutate GameObject hierarchy unless explicitly synchronized.
- Render Thread does not directly access live GameObject data.
- Render Thread does not directly access Scene Thread render objects except through captured RT proxies, render
  snapshots, or render commands.

Scene Thread and Render Thread communicate through the `RenderSystem` render command queue. Later render snapshots or
render world state may use double or triple buffering when scene-to-render synchronization needs a stable frame boundary.

Recommended frame flow:

```text
Main Thread
  Pump platform messages
  Push OS events and window state deltas to Scene Thread through OSEventQueue
  Tick application shell

Scene Thread
  Consume OS events and update input snapshot
  Update CPU-side render resources such as viewport and render target descriptions
  Update scripts
  Update components
  Update scene
  Build render commands / render snapshot, capturing RT proxies where needed

Render Thread
  Consume render commands
  Initialize/update RT proxies and RHI resources
  Build RHI command lists
  Submit to GPU queue

IO Thread + Worker Threads
  Process async loads
  Decode/import resource data
  Prepare upload requests
```

Frame pacing should keep adjacent thread stages within one frame:

- `MainThreadSceneThreadFrameEndSync` limits Main Thread lead over Scene Thread to at most one frame.
- `SceneThreadRenderThreadFrameEndSync` limits Scene Thread lead over Render Thread to at most one frame.

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

VEngine projects use a small fixed directory convention at the project root:

```text
ProjectRoot/
  VEProject.json
  Assets/
  Library/
```

`VEProject.json` is the project descriptor. It is UTF-8 JSON, written with stable indentation, and records the project
name, engine version used to create the descriptor, and the initial scene path. `Assets/` and `Library/` are fixed
code-level conventions and are not repeated in the descriptor. `Assets/` contains user-authored and imported asset
metadata such as `.vescene`, `.veasset`, `.vematerial`, `.veprefab`, `.vemesh`, and `.vetex`. `Library/` contains
generated local data, imported caches, shader/resource build products, and other machine-local files. First-stage sample
projects keep an empty `Library/.gitkeep` only so the directory shape is visible.
Project creation, descriptor editing, and workspace policy belong to the Editor layer; runtime systems only consume
explicit paths and project-root resolution supplied by higher-level code.

Repository-level engine assets are intentionally grouped under the repository `Assets/` root while remaining separate
from project-owned assets:

- `Assets/EditorOnlyAsset/`: engine-owned Editor-only resources. These are copied under the Editor's `Assets/` directory
  when the Editor is built and are not included in Player packages.
- `Assets/BuiltinAsset/`: engine-owned runtime builtin resources. These are copied under the Editor's `Assets/` directory
  for Editor use; when referenced by the runtime manifest, they are also copied into Player packages next to
  `VEnginePlayer.exe`.
- `ProjectRoot/Assets/`: project-authored resources. These stay under the project root during editing and are copied into
  Player packages when referenced by the runtime manifest.

The resource pipeline has three explicit responsibility layers:

```text
  Editor:
  EditorAssetDatabase
    -> scans ProjectRoot/Assets/ and repository runtime builtin Assets/BuiltinAsset/Engine/ when a project opens
    -> keeps asset path -> AssetID and AssetID -> asset-record indexes current
    -> tracks imported artifact path and direct dependencies
    -> updates records as editor operations create, delete, move, reimport, or save assets

  EditorResourceLoader
    -> resolves editor paths to AssetID values
    -> forwards typed requests to ResourceSystem using EditorAssetDatabase as the record provider
    -> does not own a separate ResourceObject cache

Player:
  RuntimeAssetLoader
    -> reads a packaged, read-only AssetManifest exported during build/cook
    -> resolves packaged runtime paths through the runtimePath -> AssetID map
    -> resolves AssetIDs internally through the AssetID -> ManifestAssetRecord table
    -> provides AssetRecord values to ResourceSystem
    -> never scans Assets/ and never mutates the asset manifest at runtime

Shared low-level loading:
  ResourceSystem
    -> loads files from concrete platform paths
    -> deserializes engine-native resource formats
    -> creates ResourceObject instances such as SceneResource, MeshResource, MaterialResource, and TextureResource
    -> supports public Request(assetID, provider) plus internal dependency requests
    -> tracks request/release reference counts
    -> supports root-reachability collection
```

The Editor AssetDatabase scans the project `Assets/` tree plus repository runtime builtin `Assets/BuiltinAsset/Engine/`
content, tracks native source assets and imported descriptors, and generates first-stage `.vemesh` descriptors from
`.obj` sources without copying mesh payload data. `Assets/EditorOnlyAsset/` is outside this runtime asset scan. Player
builds export a runtime asset manifest and resource payload set from the editor/tooling asset records.

Recommended asset pipeline:

```text
Source Asset
  -> AssetImporter
  -> Editor Asset Metadata / Imported Artifact
  -> Runtime AssetManifest + Packaged Resource Payload
  -> RuntimeAssetLoader
  -> ResourceSystem
```

The current Editor asset database includes a project-owned OBJ mesh import path that writes first-stage `.vemesh`
descriptors. `assimp` remains the planned route for broader source model import support:

- `fbx`.
- `gltf`.
- `glb`.
- More complete `obj` coverage than the current lightweight importer.

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

Hierarchy ownership lives on `TransformComponent` (parent/child links and child creation/destruction). `GameObject`
exposes components only; hierarchy reads and writes go through `TransformComponent`.

Recommended built-in first-stage components:

- `TransformComponent`.
- `CameraComponent`.
- `MeshRenderComponent`.
- `LightComponent`.
- `ScriptableComponent` interface.
- `DotnetScriptableComponent`.
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

`ScriptableComponent` is the engine-facing script component interface. `DotnetScriptableComponent` is the first concrete
implementation and represents scripts authored for the .NET scripting backend.

Scriptable component lifecycle:

```text
OnCreate
OnDestroy
OnEnable
OnDisable
OnUpdate
OnLateUpdate
```

Editor reload policy:

- Basic implementation target: stop scene, unload/recreate script context, reload assembly.
- Standard architecture target: allow Editor to rebuild C# project and reload script domain while preserving serialized scene data.
- Advanced runtime state-preserving hot reload is not a first-stage requirement.

macOS first-stage policy:

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
- Editor UI render backend setup enters through `Editor::InitRenderBackend()`, then dispatches by platform to
  `EditorRenderBackend` implementations such as the Windows ImGui/D3D11 backend and the macOS ImGui/Metal backend.
- Editor input setup enters through `EditorInput`, then dispatches by platform to `EditorInputBackend` implementations
  such as the Windows ImGui/Win32 backend and the macOS ImGui/AppKit backend.
- Editor project history enters through `EditorProjectRegistry`, then dispatches by platform to
  `EditorProjectRegistryBackend` implementations such as the Windows registry backend and the macOS UserDefaults backend.
- Editor Console receives logs through the VEngine logging facade callback path.
- Editor panels receive their editor context during initialization; panel rendering should not take the whole `Editor`, and cross-panel communication should flow through editor-owned events.

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
- Touch input on Apple platforms.

The runtime UI renderer should integrate with the Render system and RHI, not with ImGui.

## 17. Platform Architecture

### 17.1 Windows

Windows platform layer:

- Owns Win32 window creation.
- Owns Win32 message loop.
- Exposes native window handles.
- Supports Player and Editor shells.
- Owns debug console output and command input in debug builds.
- Produces keyboard and mouse OS events for Input and Editor consumption.
- Later supports dynamic library loading for scripting.
- Later provides platform file path utilities.

### 17.2 macOS

Current macOS platform status:

- Uses Objective-C++ for native bridge files.
- Provides an early AppKit player shell in `VEngineMacPlayer`.
- Provides a separate `VEngineRhiMetalTriangleDemo` that creates a Metal-backed view and exercises the Metal RHI.

Remaining macOS platform work:

- Route `Application` and `Window` creation through a macOS backend instead of the current Win32-only `Window::Create()`.
- Integrate `CAMetalLayer` or `MTKView` surface ownership into the normal Player path.
- Forward keyboard and mouse events to Input.
- Coordinate application activation and deactivation.
- Provide Metal drawable and surface access to `MetalRHI` through the shared RenderSystem path.

The first macOS proof is present as the Metal triangle demo. The normal macOS Player path is still an early shell,
not a complete engine loop with scene rendering and input.

## 18. Testing Strategy

Windows unit and smoke tests are registered through CMake/CTest.

Current Windows CTest targets include Memory, Math, Viewport, Scene Serialization, and Resource/Render resource tests.
Core, Logging, Time, FileSystem, Threading, JobSystem, IOSystem, RHI, ShaderTool, and packaging coverage should be added
or reintroduced as focused test executables as those surfaces stabilize.

Planned or incomplete unit test areas:

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

Planned integration test areas:

```text
Asset import smoke test
Shader compile smoke test
C# script host smoke test
```

Manual/sample checks:

```text
Windows Player demo
Windows Editor demo
macOS demo
```

RHI validation should stay focused on RenderSystem-owned runtime paths:

- Create device through RenderSystem.
- Create the main swapchain through RenderSystem.
- Exercise the minimal RenderFrame path.
- Keep standalone D3D11/D3D12 triangle demo programs out of the normal milestone flow.

## 19. CMake Build Plan

Recommended options:

```cmake
option(VE_BUILD_PLAYER "Build Windows player" ON)
option(VE_BUILD_EDITOR "Build Windows editor" ON)
option(VE_BUILD_TESTS "Build tests" ON)
option(VE_BUILD_TOOLS "Build tools" ON)
option(VE_BUILD_MAC_PLAYER "Build macOS player" OFF)

option(VE_ENABLE_D3D11 "Enable D3D11 RHI" ON)
option(VE_ENABLE_D3D12 "Enable D3D12 RHI" ON)
option(VE_ENABLE_METAL "Enable Metal RHI" OFF)
```

Recommended presets:

```text
windows-msvc-debug
windows-msvc-release
windows-msvc-tests
mac-debug
mac-release
mac-test
```

The Windows presets are the Visual Studio 2022/v143 lane. If the project adds a Visual Studio 2026 or later lane, keep it
as an explicit new preset and rebuild or revalidate the third-party payloads against that compiler baseline.

macOS may be generated through CMake/Xcode, while keeping a small amount of macOS-specific template content such as `Info.plist`, app delegate, view controller, and entitlement files.

## 20. Development Plan

The development roadmap, early technical validation items, and recommended implementation order are maintained separately in `Docs/DevelopmentPlan.md`.
