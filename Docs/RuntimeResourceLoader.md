# VEngine Runtime Resource Loader

## Purpose

`RuntimeResourceLoader` is the player-side asset indexing and resource load orchestration layer. Its role is close to
Unity's AssetBundle locator layer: gameplay code asks for a typed resource by packaged runtime path, while the loader
resolves that path through the read-only asset manifest before delegating physical loading to `ResourceSystem`.

`RuntimeResourceLoader` does not scan `Assets/`, import source files, create `.meta` files, or modify its asset manifest
at runtime. Those responsibilities belong to `EditorAssetDatabase` and build/cook tools.

## Layering

```text
RuntimeResourceLoader
  Owns:
    GUID -> AssetRecord lookup
    packaged runtimePath -> GUID lookup
    AssetManifest lifetime
    direct dependency records
    dependency load order
    player-facing typed path load entry points
    player-facing explicit release entry point

ResourceSystem
  Owns:
    concrete path resolution under project/package root
    file reads through FileSystem and future IOSystem paths
    resource file deserialization
    concrete ResourceObject creation
    low-level cache, reference counts, and resource object storage
```

Editor and player use different indexing layers but share the same low-level loader:

```text
EditorAssetDatabase + EditorResourceLoader
RuntimeResourceLoader + ResourceSystem
```

## Manifest Policy

Runtime asset manifests are exported during packaging or cooking. They contain stable asset records:

```json
{
    "guid": "33333333-3333-3333-3333-333333333333",
    "type": "Text",
    "runtimePath": "Cooked/Text/Message.txt",
    "dependencies": [],
    "contentHash": "..."
}
```

In memory, `AssetManifest` owns two indexes:

- `Guid -> AssetRecord` is the authoritative asset table.
- `runtimePath -> Guid` is the business-facing path lookup table.

Path-based loads go through the `runtimePath -> Guid` map first, then use the GUID to fetch the asset record. They do
not iterate all asset records looking for a matching path. If a record is updated with a new path, the old path index is
removed; if a new GUID reuses an existing path, that path is remapped to the new GUID so the asset manifest keeps one
owner for each runtime path.

The manifest stores direct dependencies only. `RuntimeResourceLoader` expands the dependency graph at load time, detects
cycles, loads dependencies before dependents, and returns the root resource result.

Runtime path lookup uses the `AssetRecord::runtimePath` stored in the packaged asset manifest. This supports gameplay or
business code that wants a stable string/path ID such as `Scenes/Main.vescene` or `UI/Hud.vematerial` without requiring
the caller to know the asset GUID. It remains a runtime asset manifest lookup; the player does not scan `Assets/`.

The public loading API is intentionally path-based and typed:

```cpp
Result<MeshResource*> mesh = loader.LoadResource<MeshResource>(Path("Meshes/Cube.vemesh"), resourceSystem);
Result<MaterialResource*> material =
    loader.LoadResource<MaterialResource>(Path("Materials/Default.vematerial"), resourceSystem);
```

GUID lookup remains an internal implementation detail used for dependency traversal and cache identity.

## Resource Lifetime

`RuntimeResourceLoader` owns path lookup and dependency traversal, but loaded object lifetime remains in
`ResourceSystem`. Runtime loads acquire references for the root resource and its dependencies. Runtime code must release
each resource it loaded through the loader:

```cpp
(void)loader.ReleaseResource(mesh.GetValue(), resourceSystem);
```

Releasing a root resource also releases the dependency references acquired for that load. `ResourceSystem` destroys a
cached object once its reference count reaches zero, unless editor collection currently keeps it reachable.

## Sync And Async Loading

The first public runtime loader API is synchronous and typed. Low-level async-shaped primitives may remain in
`ResourceSystem`, but `RuntimeResourceLoader` should expose typed async loading only after resource object ownership and
thread publication rules are ready.

Expected later async flow:

```text
LoadResourceAsync<MeshResource>(runtimePath)
  -> resolve dependency order from asset manifest
  -> submit file reads through IOSystem
  -> submit CPU-heavy parse/decode work through JobSystem
  -> publish loaded resource handles on the owning runtime thread
  -> submit GPU resource creation through RenderSystem when needed
```

## Relationship To EditorAssetDatabase

`EditorAssetDatabase` is the editor-side indexing layer. It scans the project, owns live asset records, updates records
during editor operations, and resolves project-relative asset paths from the editable project workspace. Editor resource
loading is handled by `EditorResourceLoader`, not `ResourceSystem`, so editor lifetime and runtime lifetime stay fully
separate.

The two layers intentionally do not share mutation rules:

- Editor records are live and mutable while editing.
- Runtime asset manifests are packaged and read-only while the player runs.
- `EditorResourceLoader` owns editor cache collection.
- `ResourceSystem` owns runtime reference counting and explicit release.
