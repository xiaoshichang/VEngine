# Windows Packaging Design

## 1. Scope

The first packaging slice adds an Editor-driven Windows package flow. It produces a runnable Windows package layout from
the currently opened project and intentionally keeps the implementation small.

Current scope:

- Platform: Windows x64 only.
- Player binary: existing `VEnginePlayer.exe` built next to the running `VEngineEditor.exe`.
- Assets: runtime asset files referenced by the Editor asset database, including required runtime files imported from
  repository-level `Assets/BuiltinAsset/Engine/` sources.
- Asset metadata: exported `AssetManifest.json`.
- Shader cross-platform handling: out of scope for this slice.
- Build execution: one packaging step is advanced from the Editor modal each frame. A background build worker can be
  added later when packaging becomes expensive.

## 2. Project Build Root

Packages are written under the project root, next to `Assets/`, `Library/`, and `VEProject.json`.
Repository-level engine assets live beside the project root under `Assets/BuiltinAsset/` and
`Assets/EditorOnlyAsset/`. `Assets/BuiltinAsset/Engine/` may feed runtime imports and packages. Editor-only engine
assets are copied with Editor builds but are not copied into Player packages.

```text
ProjectRoot/
  Assets/
  Library/
  Build/
    Logs/
    Windows/
```

`Build/` is generated local output and should not be required for source control.

## 3. Package Output Layout

Each packaging run creates a timestamped output directory. Timestamped directories keep previous packages inspectable and
make log correlation straightforward.

```text
ProjectRoot/
  Build/
    Logs/
      Package_Windows_YYYYMMDD_HHMMSS.log
    Windows/
      ProjectName_YYYYMMDD_HHMMSS/
        Bin/
          VEnginePlayer.exe
        Data/
          VEProject.json
          AssetManifest.json
          Assets/
            ...
          BuiltinAsset/
            ...
          Library/
            Imported/
              ...
        Logs/
```

Directory responsibilities:

- `Bin/`: Windows executable payload for the packaged player.
- `Data/`: project data consumed by the runtime. It preserves runtime asset paths so existing resource loading behavior
  can resolve `Assets/...`, `BuiltinAsset/...`, and `Library/...` paths.
- `Data/AssetManifest.json`: runtime manifest exported from `EditorAssetDatabase`.
- `Data/VEProject.json`: copied project descriptor for future player bootstrapping and diagnostics.
- Packaged Windows Player startup probes the sibling `Data/` directory beside `Bin/`, loads `AssetManifest.json`, then
  loads the descriptor `startScene`.
- `Logs/`: reserved for runtime logs emitted by the packaged player.
- `Build/Logs/Package_Windows_YYYYMMDD_HHMMSS.log`: per-run packaging log produced by the Editor packaging flow.

## 4. Packaging Steps

The Editor modal presents and executes these steps:

1. Prepare directories under `Build/`.
2. Refresh the asset database so imports and metadata are current.
3. Export `AssetManifest.json` into the package `Data/` directory.
4. Copy every runtime asset referenced by the manifest into `Data/` while preserving `Assets/...`, `BuiltinAsset/...`,
   and `Library/...` paths.
5. Copy `VEnginePlayer.exe` into `Bin/`.
6. Write `PackageInfo.json` with basic package metadata.

If a step fails, packaging stops, the modal shows the failed step, and the packaging log records the failure code or
exception message.

## 5. Logging

Every packaging run attempts to create a log file in `ProjectRoot/Build/Logs/` before doing package work. The log records:

- package start and end times;
- selected project path and package output path;
- every step transition;
- copied source and destination paths;
- recoverable errors returned by engine/editor systems;
- filesystem exceptions raised by the host platform.

The packaging log is separate from the engine-wide `Logs/VEngine.log` sink because packaging is a user-facing build
artifact and should remain available beside the project package outputs.

## 6. Known Follow-Ups

- Build `VEnginePlayer` automatically when the binary is missing or stale.
- Add Release/Debug configuration selection.
- Add shader artifact cooking and platform-specific shader bundles.
- Add optional cleaning or archiving of old package directories.
