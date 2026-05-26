# Editor MVP

## 1. Purpose

This document expands Milestone 7 from `Docs/DevelopmentPlan.md`.

Milestone 7 turns the Windows Editor from an empty shell into the first usable project editor. The goal is not a full
commercial editor, but a practical loop:

```text
Open VEngine project
  -> scan assets
  -> open scene
  -> inspect hierarchy and assets
  -> edit reflected component properties
  -> import source assets
  -> play and stop the scene
  -> package a basic build layout
```

The Editor should stay thin over the engine's public systems. It should use `AssetDatabase`, scene serialization,
Reflection, ResourceManager, RenderSystem, and the logging facade instead of creating separate editor-only data models
for the same concepts.

## 2. First-Stage Non-Goals

Milestone 7 should not become a complete content production environment. It should defer:

- Multi-user editing and source-control integration UI.
- Advanced undo/redo history.
- Large-scale asset dependency graph visualization.
- Asset hot reload while Play Mode is running.
- Prefab authoring beyond preserving the file format direction.
- Terrain, animation, particle, material graph, or UI authoring tools.
- Full platform cooker optimization.
- Patch generation, DLC, remote asset cache, and chunk-based streaming install.
- iOS device signing and App Store packaging. Milestone 7 defines the layout contract; Milestone 10 proves iOS runtime
  execution.

## 3. Project Identity

A VEngine project is a directory with a small root descriptor and versioned authored assets. Milestone 7 should add a
root marker file so the Editor can distinguish a project folder from an arbitrary directory.

Recommended first descriptor:

```text
<ProjectRoot>/.veproject
```

Recommended first content:

```json
{
  "format": "VEngine.Project",
  "version": 1,
  "guid": "00000000-0000-0000-0000-000000000000",
  "name": "SampleGame",
  "engineVersion": "0.1.0",
  "startupScene": {
    "guid": "00000000-0000-0000-0000-000000000000",
    "path": "Assets/Scenes/Main.vescene"
  },
  "targetPlatforms": ["Windows", "iOS"]
}
```

The descriptor should stay small. Large settings should live in project settings assets once those settings need their
own UI. The root descriptor is for opening, identifying, and bootstrapping a project.

Rules:

- `guid` is the stable project identity.
- `name` is the default display name and package name seed.
- `startupScene` is the scene loaded when pressing Play or building a default Player package.
- Stored paths are project-relative and use `/` separators.
- The Editor may accept an absolute path from command line or file dialogs, but should normalize stored paths relative
  to the project root.

## 4. Editor Startup And Project Launcher

The Windows Editor should follow a Unity-like startup flow.

When launched with an explicit project path, the Editor opens that project directly:

```text
VEngineEditor.exe --project <project-root>
```

When launched without project arguments, the Editor should open a Project Launcher screen instead of creating an empty
editing session. The launcher should list recently opened projects and offer simple actions:

```text
Open recent project
Browse existing project...
Create new project...
```

After a project is open, the main menu should still expose:

```text
File -> Open Project...
```

Project open flow:

```text
Select project root
  -> find and parse .veproject
  -> validate minimum folder structure
  -> create missing Generated subdirectories
  -> open AssetDatabase(projectRoot)
  -> scan Assets and .veasset sidecars
  -> report diagnostics in the Windows debug console
  -> open startupScene when present
  -> fall back to an empty unsaved scene when startupScene is missing
```

Validation should be helpful rather than brittle:

- Missing `.veproject` should show a clear "not a VEngine project" diagnostic.
- Missing `Assets/` may be offered as "create project structure" only when the user explicitly creates or upgrades a
  project.
- Missing generated import artifacts should surface through `AssetDatabase::Validate()` and the Asset Browser.
- Missing startup scene should not crash the Editor. It should open an empty scene and show a warning.
- Duplicate GUIDs should block normal editing until fixed, because scene references would be ambiguous.

Recent projects are machine-local Editor state and should not be stored in the project directory. On Windows, Milestone
7 should store the recent project list in the current user's registry hive:

```text
HKCU\Software\VEngine\Editor
```

Recommended first value:

```text
RecentProjects REG_SZ
```

The value may contain a compact JSON array so the Editor can store path, display name, project GUID, and last opened
time without creating a larger preference system too early:

```json
[
  {
    "path": "D:/Projects/SampleGame",
    "name": "SampleGame",
    "guid": "00000000-0000-0000-0000-000000000000",
    "lastOpenedUtc": "2026-05-26T00:00:00Z"
  }
]
```

The launcher should tolerate missing or stale registry entries. If a recent project path no longer exists or no longer
contains `.veproject`, show it as unavailable or remove it after user confirmation.

## 5. Project Directory Structure

Recommended first-stage project layout:

```text
MyGame/
  .veproject

  Assets/
    Scenes/
      Main.vescene
    Materials/
      Default.vematerial
    Models/
      Character.fbx
      Character.fbx.veasset
    Textures/
      Character_D.png
      Character_D.png.veasset
    Prefabs/
    Scripts/
    Settings/

  Generated/
    Assets/
      ImportCache/
        <asset-guid>/
          <artifact>.vemesh
    Shaders/
      Windows/
        D3D11/
        D3D12/
      iOS/
        Metal/
    Build/
      Windows/
      iOS/
    Editor/
      Workspace/
      Thumbnails/
    Logs/
    Temp/
```

Versioned project files:

- `.veproject`
- `Assets/**`
- Source asset sidecars such as `*.obj.veasset`, `*.fbx.veasset`, and `*.png.veasset`
- Authored native assets such as `.vescene`, `.vematerial`, and later `.veprefab`

Generated and ignored files:

- `Generated/**`
- Build outputs under `Build/**` when the build is inside the engine repository
- Intermediate package folders
- Editor workspace layout, thumbnail cache, and local logs

The Editor should never write generated artifacts into `Assets/` unless the user deliberately exports an authored asset.
`Assets/` is the source-controlled authoring surface; `Generated/` is disposable and rebuildable.

## 6. Generated File Placement

Generated files should have stable, platform-aware locations so imports, packaging, and runtime lookup do not invent
their own paths.

Asset import outputs:

```text
Generated/Assets/ImportCache/<asset-guid>/<artifact-name>.vemesh
Generated/Assets/ImportCache/<asset-guid>/<artifact-name>.vetex
```

Shader outputs:

```text
Generated/Shaders/Windows/D3D11/<shader-guid-or-name>/
Generated/Shaders/Windows/D3D12/<shader-guid-or-name>/
Generated/Shaders/iOS/Metal/<shader-guid-or-name>/
```

Editor-local caches:

```text
Generated/Editor/Workspace/
Generated/Editor/Thumbnails/
Generated/Logs/
Generated/Temp/
```

Package staging outputs:

```text
Generated/Build/Windows/<configuration>/<project-name>/
Generated/Build/iOS/<configuration>/<project-name>/
```

The first implementation can rebuild generated data eagerly. Later milestones can add timestamps, hashes, and dependency
graph checks to avoid unnecessary work.

## 7. Asset Browser Responsibilities

The Asset Browser is the Editor face of `AssetDatabase`.

First-stage responsibilities:

- Show the `Assets/` tree.
- Show source assets, sidecar metadata, native VEngine assets, and generated artifact status.
- Display GUID, asset type, source path, importer id, importer version, and artifact paths in a details panel.
- Run `scan`, `import`, `reimport`, and `validate` through the same backend as `VEngineAssetTool`.
- Open `.vescene` assets into the Editor scene.
- Select `.vematerial` and other inspectable assets so the Inspector can edit reflected or known JSON-backed fields.
- Surface missing generated artifacts and duplicate GUID diagnostics clearly.

The Asset Browser should not duplicate import logic. Its commands should call the shared import service used by
`VEngineAssetTool`.

## 8. Scene Editing And Play Mode

The Editor should maintain a clear edit/play split:

```text
Edit Scene
  -> press Play
  -> clone or serialize/deserialize scene into Play Scene
  -> run Game Thread update
  -> press Stop
  -> discard Play Scene
  -> return to unchanged Edit Scene
```

First-stage Play Mode may use a synchronous transition. It should still avoid mutating the edit scene with runtime-only
state. This keeps future scripting reload and undo behavior easier to reason about.

The Inspector should edit component properties through Reflection. Property changes should mark the scene dirty and
should be serialized through the same `.vescene` contract used by the Player.

## 9. Windows Packaging Layout

Milestone 7 should define a simple Windows package path even if the first packaging command is modest.

Recommended command shape:

```text
VEngineAssetTool package --project <project-root> --platform Windows --config Debug|Release --output <output-dir>
```

The Editor can expose the same backend through:

```text
File -> Package Project -> Windows
```

Recommended package flow:

```text
Open project
  -> validate .veproject
  -> refresh AssetDatabase
  -> reimport dirty source assets
  -> compile required shaders for D3D11 and D3D12
  -> build or locate VEnginePlayer.exe
  -> stage native assets and generated artifacts
  -> write read-only asset manifest
  -> copy required runtime DLLs and shader artifacts
  -> optionally launch smoke test
```

Recommended Windows package output:

```text
<OutputDir>/<ProjectName>-Windows/
  VEnginePlayer.exe
  Content/
    .veproject
    AssetManifest.veassetmanifest
    Assets/
      Scenes/
      Materials/
    Generated/
      Assets/
        ImportCache/
      Shaders/
        Windows/
          D3D11/
          D3D12/
  Logs/
```

Rules:

- Player runtime reads from `Content/` as a read-only packaged project root.
- Runtime source import is disabled.
- The manifest maps `AssetGuid` to package-relative native asset or generated artifact paths.
- Debug packages may keep text `.vemesh` and JSON assets for inspection.
- Release packages may later replace large generated payloads with binary cooked caches without changing authored asset
  GUIDs.
- User save data and runtime logs should go outside `Content/` in a platform user-data path.

## 10. iOS Packaging Layout

Milestone 7 should document the iOS package contract, while Milestone 10 proves the iOS runtime target and simulator
execution.

Recommended command shape:

```text
VEngineAssetTool package --project <project-root> --platform iOS --config Debug|Release --output <output-dir>
```

Recommended iOS package flow:

```text
Open project
  -> validate .veproject
  -> refresh AssetDatabase
  -> reimport dirty source assets
  -> compile required shaders to Metal artifacts
  -> stage content into the iOS app resource bundle
  -> generate or update the CMake/Xcode iOS player target inputs
  -> let Xcode perform simulator/device build, signing, and app bundle creation
```

Recommended app bundle resource layout:

```text
VEngineIOSPlayer.app/
  Content/
    .veproject
    AssetManifest.veassetmanifest
    Assets/
      Scenes/
      Materials/
    Generated/
      Assets/
        ImportCache/
      Shaders/
        iOS/
          Metal/
```

iOS rules:

- Bundle `Content/` is read-only at runtime.
- Runtime source import is disabled.
- Generated import artifacts and Metal shader outputs must be produced before the Xcode build packages resources.
- Runtime writable data goes to the app sandbox, not the bundle.
- The first target is iOS Simulator. Device signing, provisioning, TestFlight, and App Store packaging are later release
  engineering work.

## 11. Runtime Manifest

Packaged builds should not scan arbitrary editor project folders at startup. Packaging should emit a compact manifest:

```text
Content/AssetManifest.veassetmanifest
```

Recommended first manifest fields:

```json
{
  "format": "VEngine.AssetManifest",
  "version": 1,
  "projectGuid": "00000000-0000-0000-0000-000000000000",
  "assets": [
    {
      "guid": "00000000-0000-0000-0000-000000000000",
      "type": "Scene",
      "path": "Assets/Scenes/Main.vescene"
    }
  ]
}
```

The Editor can still use `AssetDatabase` scanning for live authoring. Player packages should prefer the manifest so
startup is deterministic and does not depend on editor-only sidecar discovery behavior.

## 12. Implementation Order

Recommended order:

- Add project descriptor parsing and validation for `.veproject`.
- Add an Editor project service that owns `projectRoot`, project descriptor data, `AssetDatabase`, and current scene
  state.
- Add command line project opening through `VEngineEditor.exe --project <project-root>`.
- Add a no-argument Project Launcher that reads recent projects from the Windows registry.
- Add `File -> Open Project...` and update the registry-backed recent project list after successful opens.
- Create missing `Generated/` subdirectories when a project opens.
- Add docking Editor shell, main menu, and project status display.
- Add Asset Browser over `AssetDatabase` scan results.
- Add scene open/save and startup scene loading.
- Add Scene Hierarchy and Inspector using Scene and Reflection APIs.
- Add Viewport rendering through the existing RenderSystem/RHI path.
- Add Play/Stop with a separate play scene instance.
- Add shared packaging service shape and a first Windows package staging path.
- Document the iOS package layout and keep staging code structured so Milestone 10 can feed the iOS app bundle.

## 13. Completion Criteria

Milestone 7 is complete when:

- The Editor can open a `.veproject` project from command line and UI.
- The project root, `Assets/`, and `Generated/` layout are created or validated consistently.
- Asset Browser reflects `AssetDatabase` state and can trigger import, reimport, scan, and validate.
- A `.vescene` can be opened, inspected, edited through Reflection, saved, and loaded again.
- The Viewport can render the currently opened scene.
- Windows Debug Console displays engine logs, asset diagnostics, and GM command input in both Debug and Release Editor
  builds.
- Play/Stop runs a separate play scene without corrupting the edit scene.
- Windows package staging can produce a read-only `Content/` layout with `.veproject`, manifest, native assets, and
  generated artifacts.
- iOS package layout and staging contract are documented and compatible with the planned Milestone 10 app bundle path.
