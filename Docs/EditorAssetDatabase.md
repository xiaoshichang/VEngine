# VEngine Editor Asset Database

## Purpose

`EditorAssetDatabase` is an Editor-layer service that scans the open project's `Assets/` directory and records project
resources. It owns project workspace policy and import metadata generation, so it intentionally lives under `Editor/`
instead of `Engine/Runtime/`.

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
