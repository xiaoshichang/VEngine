# VEngine Editor Asset Database

## Purpose

`EditorAssetDatabase` is an Editor-layer service that scans the open project's `Assets/` directory plus the repository
level `BuiltinAsset/Engine/` directory and records editable project assets and engine-owned runtime builtin assets. It
owns project workspace policy, `.meta` file creation, importer metadata, path lookup, and live editor updates.

It does not own loaded resource objects. Editor resource objects are requested from `ResourceSystem` using
`EditorAssetDatabase` as an `IAssetRecordProvider`.

## Identity And Records

Every native asset has a sidecar `.meta` file. The `.meta` file owns the stable GUID. Runtime and component references
use `AssetID`, which is `GUID + subID`; first-stage assets use `subID = 0`.

`EditorAssetRecord` wraps the common `AssetRecord` with editor workspace information:

```text
EditorAssetRecord
  AssetRecord asset
    AssetID id
    ResourceType type
    runtimePath
    dependencies
    contentHash
  asset path under Assets/ or BuiltinAsset/
  meta path
  editor asset type
  imported artifact path
```

Internally the editor database keeps the same lookup shape as the runtime manifest:

- `asset path -> AssetID` resolves project-relative paths without scanning the asset table.
- `AssetID -> EditorAssetRecord` is the authoritative editor asset record table.

Editor UI panels that need path-ordered presentation iterate `asset path -> AssetID` and resolve each ID through the
authoritative record table.

## Project Layout

```text
Assets/
  Meshes/
    Cube.obj
    Cube.obj.meta
  Materials/
    Default.vematerial
    Default.vematerial.meta
  Scenes/
    SampleScene.vescene
    SampleScene.vescene.meta
Library/
  Imported/
    11111111-1111-1111-1111-111111111111/
      Cube.vemesh
BuiltinAsset/
  Engine/
    Shaders/
      BasicMesh.veshader
      BasicMesh.veshader.meta
EditorOnlyAsset/
  Textures/
    BuiltinGizmoIcons.png
```

## Meta Files

Meta files are text JSON:

```json
{
    "version": 1,
    "guid": "11111111-1111-1111-1111-111111111111",
    "assetType": "ObjSource",
    "sourcePath": "Assets/Meshes/Cube.obj",
    "importer": "ObjMeshImporter",
    "importSettings": {}
}
```

The GUID is converted to `AssetID(guid, 0)` for first-stage records.

## OBJ Import

The first OBJ import path writes a native mesh asset under `Library/Imported/<guid>/<source-name>.vemesh`.

When the asset database sees:

```text
Assets/Meshes/Cube.obj
```

and its meta GUID is:

```text
11111111-1111-1111-1111-111111111111
```

it writes:

```text
Library/Imported/11111111-1111-1111-1111-111111111111/Cube.vemesh
```

That imported `.vemesh` file is the runtime mesh payload. Runtime resource references use `AssetID`; runtime code loads
VEngine-native assets rather than original source files.

## Refresh Flow

`EditorAssetDatabase::Refresh()` recursively scans `Assets/` and, when present, `BuiltinAsset/Engine/` beside the
project root. The scan:

1. Creates missing `.meta` files for supported native assets.
2. Reads GUIDs from existing `.meta` files.
3. Imports `.obj` sources into `Library/Imported/<guid>/<source-name>.vemesh` when the imported file is missing.
4. Builds editor asset records with `AssetID`, source path, meta path, type, runtime path, and imported path where
   applicable.

`ReimportAll()` forces all OBJ and shader imports to be regenerated. `ReimportAsset()` currently forces regeneration for
a selected `.obj` or `.veshader`; `.vematerial` and `.vescene` only refresh their records because they do not have
importers yet.

Engine builtin runtime references use the `BuiltinAsset/...` logical path prefix. Editor-only engine resources live under
`EditorOnlyAsset/...`; they are copied with the Editor build but are not scanned into the runtime manifest or exported
into Player packages. Legacy references without a known root still resolve under `Assets/`, so project-authored assets
keep the existing short-reference behavior.

## Editor Loading And Lifetime

Editor loading is:

```text
Editor path or AssetID
  -> EditorAssetDatabase finds AssetRecord
  -> ResourceSystem::Request(assetID, editorAssetDatabase)
  -> AssetRef<TResource>
```

`EditorResourceLoader` is only a small convenience wrapper around that flow. It no longer owns a separate cache.

Editor active roots are `AssetID` values collected from:

- Active scene component `AssetRef` values.
- Selected assets.
- Preview panels.
- Thumbnail jobs.
- Importer views.

The editor calls `ResourceSystem::CollectUnusedResources()` with those roots. The collection pass keeps reachable
resources and resources with nonzero request reference counts. Active loaded `AssetRef` handles also hold explicit
references until they are reset, reassigned, or destroyed.
