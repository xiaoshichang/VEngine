# VEngine Runtime Asset Loader

## Purpose

`RuntimeAssetLoader` is the player-side asset indexing layer. It owns the packaged, read-only `AssetManifest` and
resolves runtime paths or `AssetID` values to common `AssetRecord` values.

It does not load `ResourceObject` instances. Loaded resource lifetime belongs to `ResourceSystem`.

## Layering

```text
RuntimeAssetLoader
  Owns:
    AssetID -> ManifestAssetRecord lookup
    packaged runtimePath -> AssetID lookup
    AssetManifest lifetime
    direct dependency records for cook, diagnostics, and preload planning

ResourceSystem
  Owns:
    concrete resource file reads
    ResourceObject creation
    recursive Request(assetID, provider)
    request/release reference counts
    root-reachability collection
```

`RuntimeAssetLoader` implements `IAssetRecordProvider`, so `ResourceSystem` can request resources without knowing whether
records come from a player manifest or the editor asset database.

## Manifest Policy

Runtime asset manifests are exported during packaging or cooking. Version 2 records use structured `AssetID` values:

```json
{
    "assetID": {
        "guid": "33333333-3333-3333-3333-333333333333",
        "subID": 0
    },
    "type": "Scene",
    "runtimePath": "Cooked/Scenes/Main.vescene",
    "dependencies": [],
    "contentHash": "...",
    "bundle": ""
}
```

In memory, `AssetManifest` owns two indexes:

- `AssetID -> ManifestAssetRecord` is the authoritative asset table.
- `runtimePath -> AssetID` is the runtime path lookup table.

The manifest stores direct dependencies only. Normal resource loading does not require a precomputed load order; it uses
`ResourceSystem::Request(assetID, provider)` and recursive requests through `ResourceLoadContext`.

Dependency closures remain useful for build/cook steps, dependency graph views, validation, diagnostics, and future
asynchronous preload planning.

## Resource Lifetime

Runtime resource lifetime is owned by `ResourceSystem`.

Player or scene code requests resources through `ResourceSystem` with `RuntimeAssetLoader` as the record provider:

```cpp
Result<AssetRef<MeshResource>> mesh = resourceSystem.Request<MeshResource>(meshID, runtimeAssetLoader);
```

Each request increments the cached resource reference count. Releasing the root resource also releases dependency
references acquired by that resource's `Load(ResourceLoadContext&)` operation. `ResourceSystem` can also collect cached
resources by root reachability, but it never unloads resources that still have a nonzero reference count.

## Scene Loading Relationship

`SceneResource` is a resource payload containing scene serialized data. `ResourceSystem` loads it like any other
resource, but it does not create live `GameObject` instances.

`SceneSystem` owns the complete flow that turns a `SceneResource` into a live scene:

```text
Request SceneResource
  -> deserialize scene data
  -> create inactive GameObject hierarchy
  -> deserialize component AssetRefs
  -> request and bind referenced resources
  -> register render objects
  -> activate the scene
```

`SceneSerialization` remains a data-only string serialization layer.
