# VEngine Editor Asset Database

## Purpose

`EditorAssetDatabase` is an Editor-layer service that scans the open project's `Assets/` directory and records project
resources. It owns project workspace policy, import metadata generation, GUID lookup, direct dependencies, and live
editor updates, so it intentionally lives under `Editor/` instead of `Engine/Runtime/`.

Editor resource loading is performed by `EditorAssetDatabase + ResourceSystem`:

- `EditorAssetDatabase` resolves GUIDs to `ResourceRecord` values and keeps the editable resource list current.
- `ResourceSystem` receives concrete records and performs physical file loading, deserialization, and runtime object
  creation.
- Editor loading is synchronous at this layer. Asynchronous resource loading is reserved for runtime-facing loaders.
- `EditorAssetDatabase` does not own loaded resource lifetime and does not expose per-load release APIs.

This mirrors player loading, where `RuntimeResourceLoader + ResourceSystem` performs the same split against a packaged
read-only manifest.

Internally the editor database keeps the same lookup shape as the runtime manifest:

- `asset path -> Guid` resolves project-relative paths without scanning the asset table.
- `Guid -> EditorAssetRecord` is the authoritative editor asset record table.

Editor UI panels that need path-ordered presentation should iterate `asset path -> Guid` and resolve each GUID through
the authoritative record table. `EditorAssetRecord` also carries direct dependency GUIDs so editor loads can hand the
same dependency shape to `ResourceSystem` that runtime loads receive from `ResourceManifest`; first-stage importers only
reserve the field and do not yet parse every asset format's dependency list.

The first supported native asset types are:

- `.obj` source mesh files.
- `.vematerial` material descriptors.
- `.vescene` scene files.

Every native asset has a sidecar `.meta` file. The `.meta` file owns the stable asset GUID used by scenes, materials,
and future runtime resource references.

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

`.vematerial` and `.vescene` also get `.meta` files, but they do not produce imported artifacts in the first version.

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

The imported `.vemesh` file is the runtime mesh asset. It contains the mesh data needed by runtime loaders and does not
reference the source OBJ:

```json
{
    "version": 1,
    "type": "Mesh",
    "guid": "11111111-1111-1111-1111-111111111111",
    "name": "Cube",
    "vertexFormat": "Position",
    "vertices": [
        [-0.5, -0.5, -0.5],
        [0.5, -0.5, -0.5],
        [0.5, 0.5, -0.5]
    ],
    "indices": [0, 1, 2],
    "boundsCenter": [0, 0, 0],
    "boundsExtents": [0.5, 0.5, 0.5],
    "importer": "ObjMeshImporter",
    "importSettings": {},
    "submeshes": [
        {
            "name": "Cube",
            "indexStart": 0,
            "indexCount": 3
        }
    ]
}
```

Runtime resource references should use GUIDs. The runtime loads native VEngine assets, such as `.vemesh`, rather than
loading original source files directly.

## Refresh Flow

`EditorAssetDatabase::Refresh()` recursively scans `Assets/` and:

1. Creates missing `.meta` files for supported native assets.
2. Reads GUIDs from existing `.meta` files.
3. Imports `.obj` sources into `Library/Imported/<guid>/<source-name>.vemesh` when the imported file is missing.
4. Builds editor asset records with source path, meta path, GUID, type, and imported path where applicable.

`ReimportAll()` forces all OBJ imports to be regenerated. `ReimportAsset()` currently forces regeneration for a selected
`.obj`; `.vematerial` and `.vescene` only refresh their records because they do not have importers yet.

Editor operations that create, delete, move, save, or reimport assets should update the in-memory asset records
immediately and use `Refresh()` as the fallback rescan path. Player manifests are generated from these editor/tooling
records during packaging; the editor asset database itself is not used by player runtime code.

## Resource Lifetime

Editor asset records and imported artifacts have different lifetimes from loaded resource payloads:

- Asset records live in `EditorAssetDatabase` for the open project and are cleared on project close.
- Imported artifacts under `Library/` are disk caches and are not deleted by ordinary resource unloads.
- Loaded payloads live in `ResourceSystem` and can be cleared, explicitly unloaded, or collected as unused.

The editor gathers root GUIDs from the active scene and selection state when the selection changes, when switching back
to project selection, and around project opening, then calls `ResourceSystem::CollectUnusedResources()`. The collection
pass keeps loaded root resources and their already-loaded dependencies, and unloads unrelated cached payloads. Preview
panes, thumbnail jobs, and importer views should add their own active roots before they start loading resource payloads.
Reimport and delete workflows may call `ResourceSystem::UnloadResource()` for the affected GUID or clear the cache
during broader project changes.
