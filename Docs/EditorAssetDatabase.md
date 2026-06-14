# VEngine Editor Asset Database

## Purpose

`EditorAssetDatabase` is an Editor-layer service that scans the open project's `Assets/` directory and records project
resources. It owns project workspace policy and import metadata generation, so it intentionally lives under `Editor/`
instead of `Engine/Runtime/`.

The first supported asset types are:

- `.obj` source mesh files.
- `.vemesh` imported mesh descriptors.
- `.vematerial` material descriptors.
- `.vescene` scene files.

## OBJ Import

The first OBJ import path does not copy mesh payload data. When the asset database sees:

```text
Assets/Meshes/Cube.obj
```

it creates the imported descriptor:

```text
Assets/Meshes/Cube.vemesh
```

The `.vemesh` file references the source OBJ:

```json
{
    "schemaVersion": 1,
    "type": "Mesh",
    "sourceAsset": "Assets/Meshes/Cube.obj",
    "importer": "ObjMeshImporter",
    "importSettings": {}
}
```

This keeps the first-stage workflow simple: the native source asset remains the source of truth, and the imported asset
is a JSON descriptor that later runtime/resource code can resolve.

## Refresh Flow

`EditorAssetDatabase::Refresh()` does two passes:

1. Recursively scan `Assets/` for `.obj` files and create missing `.vemesh` descriptors.
2. Recursively scan supported files and build asset records for `.obj`, `.vemesh`, `.vematerial`, and `.vescene`.

Asset records contain the project-relative path, the asset type, whether it is imported, and the source asset path for
imported mesh descriptors.
