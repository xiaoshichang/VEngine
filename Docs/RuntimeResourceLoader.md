# VEngine Runtime Resource Loader

## Purpose

`RuntimeResourceLoader` is the player-side resource indexing and load orchestration layer. It owns the packaged,
read-only `ResourceManifest` produced by editor/build tooling and resolves GUID-based or path-based load requests before
delegating physical file loading to `ResourceSystem`.

`RuntimeResourceLoader` does not scan `Assets/`, import source files, create `.meta` files, or modify its manifest at
runtime. Those responsibilities belong to `EditorAssetDatabase` and build/cook tools.

## Layering

```text
RuntimeResourceLoader
  Owns:
    GUID -> ResourceRecord lookup
    packaged runtimePath -> ResourceRecord lookup
    ResourceManifest lifetime
    direct dependency records
    dependency load order
    player-facing sync/async load entry points

ResourceSystem
  Owns:
    concrete path resolution under project/package root
    file reads through FileSystem and future IOSystem paths
    resource file deserialization
    concrete runtime object creation
    low-level cache and resource handle storage
```

Editor and player use different indexing layers but share the same low-level loader:

```text
EditorAssetDatabase + ResourceSystem
RuntimeResourceLoader + ResourceSystem
```

## Manifest Policy

Runtime manifests are exported during packaging or cooking. They contain stable resource records:

```json
{
    "guid": "33333333-3333-3333-3333-333333333333",
    "type": "Text",
    "runtimePath": "Cooked/Text/Message.txt",
    "dependencies": [],
    "contentHash": "..."
}
```

In memory, `ResourceManifest` owns two indexes:

- `Guid -> ResourceRecord` is the authoritative resource table.
- `runtimePath -> Guid` is the business-facing path lookup table.

Path-based loads go through the `runtimePath -> Guid` map first, then use the GUID to fetch the resource record. They do
not iterate all resource records looking for a matching path. If a record is updated with a new path, the old path index
is removed; if a new GUID reuses an existing path, that path is remapped to the new GUID so the manifest keeps one owner
for each runtime path.

The manifest stores direct dependencies only. `RuntimeResourceLoader` expands the dependency graph at load time, detects
cycles, loads dependencies before dependents, and returns the root resource result.

Runtime path lookup uses the `ResourceRecord::runtimePath` stored in the packaged manifest. This supports gameplay or
business code that wants a stable string/path ID such as `Scenes/Main.vescene` or `UI/Hud.vematerial` without requiring
the caller to know the asset GUID. It remains a runtime manifest lookup; the player does not scan `Assets/`.

## Resource Lifetime

`RuntimeResourceLoader` owns GUID lookup and dependency traversal, but loaded payload lifetime remains in
`ResourceSystem`. The current skeleton delegates explicit unload and unused-resource collection to `ResourceSystem`.
Future player loading should add deterministic handle or reference-count ownership on top of this layer so releasing a
root handle also releases dependency ownership when no other root uses it.

## Sync And Async Loading

The first skeleton provides both synchronous and asynchronous-shaped APIs. The async API currently completes immediately
through the same low-level path. This keeps call sites and ownership boundaries stable while leaving room for the later
IOSystem and JobSystem integration.

Expected later async flow:

```text
LoadResourceAsync(guid)
  -> resolve dependency order from manifest
  -> submit file reads through IOSystem
  -> submit CPU-heavy parse/decode work through JobSystem
  -> publish loaded resource handles on the owning runtime thread
  -> submit GPU resource creation through RenderSystem when needed
```

## Relationship To EditorAssetDatabase

`EditorAssetDatabase` is the editor-side equivalent indexing layer. It scans the project, owns live asset records,
updates records during editor operations, and resolves GUIDs from the editable project workspace. It can call
`ResourceSystem` with a concrete `ResourceRecord` when editor code needs to preview or inspect a resource payload.

The two layers intentionally do not share mutation rules:

- Editor records are live and mutable while editing.
- Runtime manifests are packaged and read-only while the player runs.
- `ResourceSystem` is shared and should not contain editor/player policy.
