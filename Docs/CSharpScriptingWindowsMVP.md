# C# Scripting Windows MVP

This document refines `DevelopmentPlan.md` Milestone 8. It defines the first Windows-only C# scripting vertical slice
for VEngine, with enough detail to implement it without turning scripting into a framework that owns the engine.

The MVP keeps the existing `GameObject + Component` scene model in charge. Managed code is a gameplay extension layer
driven by native lifecycle dispatch on the Game Thread.

## 1. Goals

- Host .NET from the Windows Player and Windows Editor as part of the always-built scripting module.
- Load one project script assembly plus the handwritten `VEngine.ScriptAPI` managed wrapper.
- Represent script behaviours through a native `ScriptComponent` that participates in Reflection and scene
  serialization.
- Dispatch `OnCreate`, `OnDestroy`, `OnEnable`, `OnDisable`, and `OnUpdate` from native scene lifecycle code.
- Expose a small, handwritten managed API for `GameObject`, `Component`, `Transform`, `Time`, and `Log`.
- Allow scripts to call a native C ABI bridge instead of reaching into private engine C++ types.
- Support Editor rebuild and reload only after Play mode is stopped.
- Add CTest smoke coverage that proves native hosting, managed lifecycle dispatch, native logging callbacks, and reload.
- Package Windows script artifacts into the project `Content/` layout established by Milestone 7.

## 2. Non-Goals

- iOS runtime scripting and Windows-style reload. iOS C# support is deferred to a separate AOT-based gameplay module
  feasibility milestone.
- Runtime hot reload while a play scene is running.
- Preserving live managed object state across reload.
- Automatic binding generation.
- Managed field serialization and Inspector editing.
- User script debugging integration.
- Managed job scheduling or managed worker-thread access to live scene state.
- Direct RHI, RenderSystem, or platform access from managed scripts.
- A custom C# build system. Use the .NET SDK through explicit CMake or Editor commands.

## 3. Module Boundaries

Native scripting code should live under a new runtime module:

```text
Engine/Runtime/Scripting/
  DotNetHost.h
  DotNetHostWindows.cpp
  DotNetHostStub.cpp
  ScriptBridge.h
  ScriptBridge.cpp
  ScriptComponent.h
  ScriptComponent.cpp
  ScriptContext.h
  ScriptContext.cpp
  ScriptHost.h
  ScriptHost.cpp
  ScriptProject.h
  ScriptProject.cpp
  ScriptTypes.h
```

Managed support code should be authored separately from engine C++:

```text
Managed/VEngine.ScriptAPI/
  VEngine.ScriptAPI.csproj
  Component.cs
  GameObject.cs
  Log.cs
  Quaternion.cs
  ScriptBehaviour.cs
  Time.cs
  Transform.cs
  Vector3.cs

Examples/AssetPipelineSample/Scripts/
  VEngine.SampleScripts/
    VEngine.SampleScripts.csproj
    RotateAndLog.cs
```

Tests should keep tiny managed test assemblies out of runtime source:

```text
Tests/Scripting/
  SmokeScripts/
    VEngine.SmokeScripts.csproj
    LifecycleProbe.cs
```

Keep `VEngine` as a static library. The scripting module is part of `VEngine`, but its Windows hosting files are only
compiled on Windows. Non-Windows builds compile a small unsupported host stub behind the same public facade until each
platform gets its own script host.

## 4. Build And Dependency Discovery

The scripting module is always part of the engine build. Milestone 8 should make Windows native hosting discovery
explicit:

- Add a CMake helper such as `CMake/SetupDotNetHosting.cmake`.
- Find a Windows x64 .NET SDK through `DOTNET_ROOT`, the `dotnet` command, or an explicit cache variable such as
  `VE_DOTNET_ROOT`.
- Locate native hosting headers and the `nethost` import library from the installed SDK or runtime packs.
- Create an imported target, for example `VEngineDotNetHosting`, instead of adding global include or link directories.
- Fail CMake configure with a clear diagnostic on Windows when required hosting files are missing.
- Compile a non-Windows stub that reports `Unsupported` until the iOS AOT script-host milestone lands.
- Add generated managed outputs under `Generated/Scripts/Windows/<Configuration>/`.
- Copy managed `.dll`, `.pdb`, `.deps.json`, and `.runtimeconfig.json` files needed by the host into that generated
  output folder.

The first-stage Windows package uses framework-dependent deployment for scripts. That means the package includes VEngine
script assemblies and runtime config files, while the target machine must provide a compatible .NET runtime. A future
packaging milestone can evaluate app-local runtime deployment after the MVP host path is stable.

## 5. Project Script Contract

Extend `.veproject` with an optional script section. Keep authored paths project-relative, and keep generated paths out
of the descriptor:

```json
{
  "scripting": {
    "windows": {
      "project": "Scripts/VEngine.SampleScripts/VEngine.SampleScripts.csproj",
      "assemblyName": "VEngine.SampleScripts"
    }
  }
}
```

Rules:

- `scripting.windows.project` points to the authored C# project file under the project root.
- `scripting.windows.assemblyName` is the managed assembly name used to resolve generated output.
- Missing `scripting` means the project has no user scripts.
- Malformed script configuration is a project descriptor error. Missing build outputs are Editor, Player, or Package
  diagnostics at the point scripts are required.
- Generated script output belongs under `Generated/Scripts/Windows/<Configuration>/`.
- Windows package staging copies script output into `Content/Scripts/Windows/`.

Do not make script source files first-class assets in this milestone. Script source is project source, while
`ScriptComponent` references managed type names serialized in scenes.

## 6. Runtime Ownership

Add a small `ScriptSystem` or `ScriptHost` service owned by `EngineRuntime`. It should sit below scene lifecycle
dispatch and above the raw .NET hosting details.

Recommended initialization order:

```text
JobSystem
  -> IOSystem
  -> ResourceManager
  -> RenderSystem
  -> ScriptHost
  -> GameThreadSystem
```

Recommended shutdown order is the reverse, with `GameThreadSystem` stopped before script contexts are destroyed.

Ownership rules:

- `DotNetHost` owns loading `hostfxr` and retrieving runtime delegates.
- `ScriptHost` owns the process-level managed runtime bridge.
- `ScriptContext` owns one loaded project script assembly and the managed instances for one active play session.
- `ScriptComponent` owns only a native script handle, serialized type identity, and component-local state.
- Scene lifecycle code drives `ScriptComponent`; managed code does not own native scene ticking.
- The Render Thread never calls managed code.
- Worker Threads do not mutate live managed script instances unless a later milestone introduces explicit scheduling.

The .NET runtime itself should be treated as process-lifetime after it is initialized. Reload should unload the project
script assembly through a managed collectible context and recreate the VEngine script context, not attempt to unload and
reload the whole CLR inside the same process.

## 7. Native Hosting Flow

The first host path should be explicit and easy to diagnose:

```text
ScriptHost::Initialize
  -> DotNetHost::LoadHostFxr
    -> get_hostfxr_path from nethost
    -> LoadLibraryW(hostfxr)
    -> bind hostfxr_initialize_for_runtime_config
    -> bind hostfxr_get_runtime_delegate
    -> bind hostfxr_close
  -> hostfxr_initialize_for_runtime_config(project runtimeconfig)
  -> hostfxr_get_runtime_delegate(load_assembly_and_get_function_pointer)
  -> load VEngine.ScriptAPI bootstrap entry points
  -> pass native bridge table to managed bootstrap
```

Native hosting should return `Result<T>` or `ErrorCode` values with clear diagnostics. Avoid silent fallback when the
SDK, runtime config, managed DLL, or bootstrap method is missing.

Minimum native entry points requested from managed code:

- `InitializeHost`
- `LoadProjectAssembly`
- `CreateScriptInstance`
- `DestroyScriptInstance`
- `InvokeLifecycle`
- `UnloadProjectAssembly`

Minimum native bridge calls exposed to managed code:

- `Log(severity, message)`
- `GetDeltaSeconds()`
- `GetTotalSeconds()`
- `GetFrameIndex()`
- `GetGameObjectName(componentHandle)`
- `SetGameObjectName(componentHandle, name)`
- `GetTransformLocalPosition(componentHandle)`
- `SetTransformLocalPosition(componentHandle, value)`
- `GetTransformLocalRotation(componentHandle)`
- `SetTransformLocalRotation(componentHandle, value)`
- `GetTransformLocalScale(componentHandle)`
- `SetTransformLocalScale(componentHandle, value)`

Use opaque handles at the ABI boundary. Managed code should not receive raw C++ object pointers that it can dereference.

## 8. Managed API Shape

The handwritten managed API should be intentionally small:

```text
VEngine
  Log
  Time
  Vector2
  Vector3
  Quaternion

VEngine.Scripting
  Component
  GameObject
  ScriptBehaviour
  Transform
```

First-stage `ScriptBehaviour`:

```csharp
namespace VEngine.Scripting;

public abstract class ScriptBehaviour : Component
{
    protected virtual void OnCreate() {}
    protected virtual void OnDestroy() {}
    protected virtual void OnEnable() {}
    protected virtual void OnDisable() {}
    protected virtual void OnUpdate(float deltaTime) {}
}
```

The native `Component::OnUpdate()` has no parameters today. `ScriptComponent` should read `Time::GetDeltaSeconds()` and
pass the value to managed `OnUpdate(float deltaTime)`.

API constraints:

- Managed `GameObject` and `Transform` objects are wrappers around opaque native component handles.
- Managed wrappers should validate stale handles and log clear errors after a script instance is destroyed.
- `Log` routes through the existing VEngine logging facade.
- `Time` reads the engine time snapshot owned by the Game Thread.
- Input is deferred until the engine has a stable input snapshot. Do not add raw Win32 input just for scripts.
- Public managed APIs should avoid exposing native threading, rendering, or resource internals in the MVP.

## 9. ScriptComponent

`ScriptComponent` is the scene-facing native component that binds a GameObject to one managed behaviour type.

Minimum native properties:

- `ScriptTypeName`: full managed type name, for example `Game.Rotator`.
- `AssemblyName`: optional override. Empty means use the project default script assembly.
- `IsScriptValid`: runtime-only diagnostic state, not serialized.
- `LastError`: runtime-only diagnostic text, not serialized.

Minimum serialized scene shape:

```json
{
  "type": "ScriptComponent",
  "properties": {
    "scriptTypeName": "Game.Rotator",
    "assemblyName": ""
  }
}
```

Lifecycle behavior:

- `OnCreate` asks the active `ScriptContext` to create a managed instance for `ScriptTypeName`.
- `OnEnable` dispatches managed `OnEnable` after the managed instance exists.
- `OnDisable` dispatches managed `OnDisable` if the instance exists.
- `OnUpdate` dispatches managed `OnUpdate(deltaTime)` only when the component is active, enabled, and valid.
- `OnDestroy` dispatches managed `OnDestroy` and releases the managed instance.
- Missing type, missing assembly, or managed constructor failure marks the component invalid and logs once.

Reflection behavior:

- Register `ScriptComponent` like other scene components.
- Expose `ScriptTypeName` and `AssemblyName` to Inspector editing.
- Keep managed public field reflection out of the MVP.
- Scene serialization should continue to skip unknown component types gracefully.

## 10. Editor Workflow

Milestone 8 reload is deliberately conservative:

```text
Stop Play Mode
  -> destroy play scene script instances
  -> run dotnet build for the project script csproj
  -> copy generated managed outputs
  -> unload project script AssemblyLoadContext
  -> create a new ScriptContext
  -> Play Mode may start again
```

Editor rules:

- Do not rebuild or reload while Play mode is running.
- `StartPlayMode` creates a play scene instance and binds it to a fresh `ScriptContext`.
- `StopPlayMode` destroys the play scene, calls script destroy lifecycle, and releases managed instances.
- A failed C# build surfaces diagnostics through the Editor project diagnostics and debug console.
- A failed reload keeps the edit scene open and prevents Play mode from starting until the script context is valid.
- Runtime state is not copied across reload. Serialized scene data is the source of truth.

The first Editor command can be backend-only. A full Console panel, clickable diagnostics, and managed stack navigation
can follow after the scripting host is reliable.

## 11. Player Workflow

Windows Player startup should use the opened or packaged project descriptor:

```text
Open project or Content/
  -> load .veproject
  -> load asset database or package manifest
  -> initialize ScriptHost if scripts are configured
  -> load startup scene
  -> create ScriptContext for the active scene
  -> run Game Thread lifecycle
```

Failure policy:

- If a scene contains `ScriptComponent` but no `ScriptContext` is bound, report a clear component error instead of
  silently leaving the component inert.
- If required runtime files are missing, Player startup should fail with a clear error.
- If one script type fails, keep the scene running where possible and disable only the failed component.
- Unhandled managed exceptions should be caught at the native boundary, logged with managed stack text, and disable the
  throwing script instance to avoid logging the same exception every frame.

## 12. Packaging

Extend the Windows package staging from Milestone 7:

```text
Content/
  .veproject
  AssetManifest.veassetmanifest
  Assets/
  Generated/
  Scripts/
    Windows/
      VEngine.ScriptAPI.dll
      VEngine.ScriptAPI.pdb
      VEngine.SampleScripts.dll
      VEngine.SampleScripts.pdb
      VEngine.SampleScripts.deps.json
      VEngine.SampleScripts.runtimeconfig.json
```

Package validation should report:

- Missing `scripting.windows.project` when the scene has script components that require a project assembly.
- Missing generated script DLL.
- Missing runtime config.
- Script assembly name mismatch.
- Script components whose type names cannot be resolved from the packaged assembly.

## 13. Testing

Add script-host tests only when the required .NET hosting files are available.

Recommended CTest coverage:

- `VEngineScriptingTests` for native host path, missing file diagnostics, bridge table setup, lifecycle dispatch,
  exception disabling, and reload-after-stop behavior.
- Editor project tests for descriptor parsing and the rule that scripts cannot rebuild while Play mode is running.
- Package tests for Windows script payload staging, manifest metadata, iOS exclusion, and missing payload diagnostics.
- A managed smoke project under `Tests/Scripting/SmokeScripts` that logs from C#, increments lifecycle counters, and
  mutates a Transform through `VEngine.ScriptAPI`.

Smoke success criteria:

- Native C++ initializes .NET through `nethost` and `hostfxr`.
- A managed script instance receives `OnCreate`, `OnEnable`, `OnUpdate`, `OnDisable`, and `OnDestroy` in order.
- C# can call native logging through the bridge.
- C# can read `Time.DeltaTime`.
- C# can move a native `TransformComponent`.
- Destroying the play scene releases all managed script instances.
- Stopping, rebuilding, reloading, and starting Play mode again calls the new assembly instance.

## 14. Implementation Order

1. Add CMake discovery for .NET native hosting and build the managed scripting targets on Windows.
2. Add platform stubs so non-Windows builds keep compiling cleanly.
3. Add `DotNetHost` with explicit hostfxr loading and diagnostics.
4. Add the `VEngine.ScriptAPI` managed project and bootstrap entry points.
5. Add `ScriptBridge` and opaque native handle tables.
6. Add `ScriptHost` and `ScriptContext` for project assembly load, instance creation, lifecycle dispatch, and teardown.
7. Add `ScriptComponent` and register it with Reflection.
8. Extend scene serialization tests to cover `ScriptComponent`.
9. Add Editor project script configuration parsing and generated script directories.
10. Add backend Editor rebuild/reload command that requires Play mode to be stopped.
11. Add Player startup binding for configured script assemblies.
12. Extend Windows package staging to include script artifacts.
13. Add native and managed CTest smoke coverage.
14. Add the sample script project and one scripted object in the bundled sample project.

## 15. Completion Criteria

Milestone 8 is complete when:

- Windows builds fail during configure with actionable diagnostics when .NET native hosting dependencies are missing.
- Windows builds include the managed API project and native scripting host.
- Non-Windows builds include scripting stubs until platform-specific hosts are implemented.
- Windows Player can load a project scene with a `ScriptComponent` and run C# lifecycle methods.
- Windows Editor can stop Play mode, rebuild scripts, reload the script context, and start Play mode again.
- C# scripts can log, read time, and mutate Transform through the handwritten API.
- Script failures produce actionable diagnostics without crashing the whole process in ordinary user-script cases.
- Windows package staging includes managed script artifacts and validates missing script payloads.
- CTest covers native host smoke, lifecycle dispatch, bridge calls, serialization, and reload-after-stop.
