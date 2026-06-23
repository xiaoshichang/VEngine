# VEngine Repository Guidance

## Project Shape

VEngine is a cross-platform lightweight 3D mini game engine. Keep the engine architecture small, explicit, and extensible rather than framework-driven.

Canonical docs:

- `Docs/ArchitectureOverview.md` contains the architecture overview.
- `Docs/DevelopmentPlan.md` contains feasibility checks, milestones, and implementation order.
- `Docs/CodingStyle.md` contains the coding style.

## Core Constraints

- Use C++20 for engine code.
- Use CMake as the project build system.
- Build core runtime logic as the `VEngine` static library.
- Windows x64 application outputs are `VEnginePlayer` and `VEngineEditor`; Windows tests are registered as individual
  CTest executables.
- iOS outputs include `VEngineIOSPlayer` and the first iOS Simulator Metal demo target.
- Use namespace `ve` for engine code.

## Build Commands

On Windows, run CMake configure, build, and test commands through `CMake/Scripts/WithMsvc.bat` so the MSVC x64 developer environment is initialized first.

Examples:

```text
CMake/Scripts/WithMsvc.bat cmake --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat cmake --build --preset windows-msvc-debug
CMake/Scripts/WithMsvc.bat ctest --preset windows-msvc-tests
```

## Coding Style

Follow `Docs/CodingStyle.md` and `.clang-format`.

Key defaults:

- C++20.
- 4 spaces, no tabs, 160-column limit.
- Allman braces.
- Indent declarations and definitions inside namespaces.
- `PascalCase` types, functions, constants, files, and CMake targets.
- `lowerCamelCase` locals and parameters.
- Trailing underscore for member fields.
- `VE_UPPER_SNAKE_CASE` macros.
- `.h` / `.cpp` for C++, `.mm` for Objective-C++, `.metal` for Metal shaders.
- Use `#pragma once`.
- Use `Boost.Log` only through the VEngine logging facade.

## Platform Policy

- Keep the platform layer self-owned by the engine.
- Windows platform code should use Win32 directly.
- iOS platform code should use Objective-C++ with UIKit and Metal surface integration.
- Do not introduce SDL, GLFW, Qt, or similar framework-style libraries that take over the main loop, windowing, platform layer, or application architecture.
- Isolate platform-specific code under platform-specific modules and keep cross-platform engine code clean.

## Third-Party Policy

Independent libraries are allowed when they do not own engine architecture.

Approved first-stage dependencies:

- Boost, including Boost.Log, Boost.JSON, Boost.UUID, Boost.Container, Boost.Lockfree, and Boost.Stacktrace.
- assimp for source model import.
- Dear ImGui for the Windows Editor only.
- FreeType for runtime UI font rendering.
- DirectXShaderCompiler for HLSL compilation.
- SPIRV-Cross for SPIR-V to MSL conversion and shader reflection.

Logging must use Boost.Log through a VEngine logging facade. Do not add spdlog.

Manage project dependencies through `ThirdParty/` CMake wrappers and download scripts. Do not add vcpkg manifest
integration. After cloning on Windows, `ThirdParty/Setup_Windows64.bat` should prepare the project-owned dependency
payloads.

## Runtime Architecture

Use a traditional `GameObject + Component` scene model with a lightweight reflection system for serialization, editor inspection, component factories, and script binding.

Keep these conceptual threads distinct:

- Main Thread: platform messages, lifecycle, application shell, editor loop.
- Game Thread: scene update, components, scripts, input consumption.
- Render Thread: render command consumption, render world update, RHI submission.
- IO Thread: file reads and async IO scheduling.
- Worker Threads: Job System work.

Main Thread and Game Thread may be physically combined in the first implementation, but code should not assume they are always the same.

Game Thread must not call RHI directly. Render Thread must not directly access live `GameObject` state. Use render commands, render proxies, or snapshots for cross-thread rendering data.

## Rendering Policy

Design the common RHI around modern explicit API concepts:

- Device
- Queue
- CommandList
- Fence
- Swapchain
- Buffer
- Texture
- Sampler
- Shader
- PipelineState
- DescriptorSet or BindGroup
- RenderPass
- ResourceState

Implement D3D12 and Metal naturally against this model. Implement D3D11 as a compatibility backend that emulates explicit concepts internally.

First-stage rendering scope:

- Static mesh
- Camera
- Transform
- Material
- Texture2D
- Directional light
- Forward rendering
- Depth buffer
- Basic shader pipeline
- Editor viewport rendering

First-stage rendering non-goals:

- Full PBR
- Deferred rendering
- Shadows
- Post-processing
- Skeletal animation
- Compute pipeline

## Shader Policy

Use HLSL as the source shader language.

Expected pipeline:

```text
HLSL
  -> DirectXShaderCompiler
    -> DXIL for D3D12
    -> D3D11-compatible output path
    -> SPIR-V
      -> SPIRV-Cross
        -> MSL for Metal
        -> reflection metadata
```

Prefer offline shader compilation through `VEngineShaderTool`. Runtime should load compiled shader artifacts and reflection metadata rather than performing full cross-compilation.

## Asset And Resource Policy

Editor and tools import source assets. Runtime loads VEngine-native assets.

Use mostly text-friendly formats early:

- `.veasset` for asset metadata.
- `.vescene` for scene data.
- `.vematerial` for material data.
- `.veprefab` for prefab data.

Large mesh and texture data may start simple, but leave room for binary cooked caches later.

## Editor And UI Policy

- Use Dear ImGui for the Windows Editor.
- Keep Editor UI separate from runtime UI.
- Runtime UI is engine-owned and should support Canvas, RectTransform, Image, Label, Button, FreeType text, and screen-space input.
- Editor should operate through Scene, Reflection, AssetDatabase, Resource, Render, and logging facades instead of reaching into private runtime internals.

## Scripting Policy

Use C# scripting on Windows through .NET native hosting:

- `nethost` locates `hostfxr`.
- `hostfxr` initializes the .NET runtime.
- C++ exposes a C ABI native bridge.
- C# uses a handwritten `VEngine.ScriptAPI` wrapper first.

Initial script lifecycle:

- `OnCreate`
- `OnDestroy`
- `OnEnable`
- `OnDisable`
- `OnUpdate`

iOS C# scripting is not a first-stage requirement. Treat iOS scripting as a future AOT feasibility milestone.

## Testing Policy

Use CMake/CTest for Windows unit and smoke tests. Test executables may use simple project-owned assertions until a
dedicated test framework is justified.

Prioritize tests for:

- Core
- Math
- Memory
- Threading
- Reflection
- Scene serialization
- Resource metadata
- FileSystem path behavior

Use smoke tests for:

- D3D11 RHI
- D3D12 RHI
- Shader compilation
- Asset import
- C# script hosting

## Implementation Bias

- Keep changes scoped to the current milestone or module.
- Preserve module boundaries from the architecture overview.
- Avoid broad refactors before the skeleton and vertical slices are stable.
- Prefer small working vertical slices over large incomplete systems.
- Update the relevant docs when an architectural decision changes.
