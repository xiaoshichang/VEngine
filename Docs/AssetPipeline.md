# Asset Pipeline

## 1. Purpose

This document expands Milestone 6 from `Docs/DevelopmentPlan.md`.

Milestone 6 turns the code-constructed resources and in-memory scene serialization from Milestone 5 into a first
file-backed asset pipeline. The target is a small but complete vertical slice:

```text
Source model or authored VEngine asset
  -> AssetDatabase
  -> import or validation
  -> VEngine-native asset files
  -> async runtime loading
  -> ResourceManager
  -> scene-driven rendering
```

The milestone should prove that VEngine can import a simple static model, author or load a material, load a
`.vescene` file that references those assets, and render the result through the existing Game Thread, ResourceManager,
RenderSystem, and RHI path. Runtime code should load VEngine-native assets and should not parse arbitrary source model
formats.

## 2. First-Stage Non-Goals

This milestone should stay focused on the first usable asset flow. It does not include:

- A production-grade binary cooker.
- Platform-specific texture compression.
- Full texture decoding and GPU texture upload.
- Full material binding or a material graph.
- Skeletal meshes, animation clips, skinning, morph targets, or animation retargeting.
- Prefab authoring.
- Asset hot reload while the Player or Editor is running.
- File watching.
- Drag-and-drop asset UI.
- Full Asset Browser UI. That belongs to the Editor MVP rather than this asset-pipeline milestone.
- Large-scale dependency graph optimization.
- Build packaging, patching, or remote asset cache support.
- Importing source assets at runtime in Player builds.

Small placeholders are allowed when they keep file contracts extensible, but they should not expand the milestone into a
complete commercial asset system.

## 3. System Ownership

The first asset pipeline keeps source import, asset metadata, and runtime resource loading as separate concerns:

```text
VEngineAssetTool
  -> AssetImporterRegistry
  -> AssetDatabase
  -> Native asset writers

Windows Editor import command
  -> AssetImporterRegistry
  -> AssetDatabase
  -> Native asset writers

Runtime Player
  -> read-only asset lookup
  -> IOSystem
  -> JobSystem
  -> ResourceManager
  -> RenderSystem synchronization
```

`AssetDatabase` owns project asset identity, asset metadata, path lookup, dependency records, generated artifact
records, and validation. It should live in the engine resource/asset layer so tools and the Editor use the same
metadata rules.

Source importers own source-format parsing. The first implementation can use a tiny project-owned OBJ importer to prove
the full chain with minimal dependency work. The assimp model importer should be added behind the same importer boundary
for `fbx`, `gltf`, and `glb` once the ThirdParty wrapper is prepared. Player runtime loading should not require source
importer code.

`ResourceManager` owns runtime resource handles, loaded resource data, fallback resources, lifetime tracking, and the
resource revisions consumed by `RenderSystem::SynchronizeRenderResources()`. `ResourceManager` may use `IOSystem` for
file reads and `JobSystem` for parsing or CPU processing, but loaded data should enter live runtime state at a
controlled Game Thread boundary.

`AssetGuid` and runtime `ResourceId` are distinct:

- `AssetGuid` is stable project identity stored in asset files and metadata.
- `ResourceId` is a runtime-local handle identity allocated by `ResourceManager`.

This distinction lets assets move on disk without changing scene references, while runtime handles remain cheap and
process-local.

## 4. Project Paths

The first implementation should use project-relative paths with `/` separators in serialized files. Absolute paths may
be accepted by tools as command line input, but stored metadata should be normalized relative to the project root when
the file lives inside the project.

Recommended first-stage layout:

```text
Assets/
  Engine/
  Samples/
    Scenes/
    Materials/
    Models/
    Textures/

Generated/
  Assets/
    ImportCache/
```

`Assets/` contains source assets and authored VEngine assets that are intended to be versioned. `Generated/Assets/`
contains imported artifacts and cache files. Generated artifacts may be ignored by git unless a sample needs to be
checked in deliberately for a smoke test or demo.

Native authored files such as `.vescene` and `.vematerial` store their own GUID. Non-native source files use a sidecar
metadata file whose name preserves the source extension, for example:

```text
Assets/Samples/Models/Crate.obj
Assets/Samples/Models/Crate.obj.veasset
Generated/Assets/ImportCache/<asset-guid>/CrateMesh.vemesh
```

The sidecar form avoids collisions between source files such as `Crate.obj`, `Crate.fbx`, and `Crate.png`.

## 5. File Format Rules

All first-stage asset files should be text-friendly JSON unless a specific payload becomes too large to inspect. Use
Boost.JSON through a thin VEngine serialization wrapper rather than ad hoc string building.

Common rules:

- Include `format`, `version`, `guid`, and `assetType` where the file is a directly addressable asset.
- Store references by GUID and include an optional project-relative path as a diagnostic fallback.
- Treat file paths as case-preserving but compare normalized project paths consistently.
- Use explicit version numbers and reject unsupported major versions with a clear log message.
- Preserve unknown fields where practical only after a deliberate policy exists. The first version may rewrite known
  fields only.
- Keep large future binary caches separate from human-authored metadata.

### 5.1 `.veasset`

`.veasset` is the metadata contract for non-native source files and importer output.

Required first-stage fields:

```json
{
  "format": "VEngine.AssetMetadata",
  "version": 1,
  "guid": "00000000-0000-0000-0000-000000000000",
  "assetType": "SourceModel",
  "source": "Assets/Samples/Models/Crate.obj",
  "importer": "ObjModel",
  "importerVersion": 1,
  "settings": {},
  "artifacts": [],
  "dependencies": []
}
```

`.veasset` should remain stable source metadata: GUID, source path, importer identity, importer settings, generated
artifact records, and dependency records. Build-time import state such as source hashes and importer-result freshness
belongs under `Generated/Assets/ImportCache/<asset-guid>/`, not in the authored sidecar.

Artifact records should describe generated native assets:

```json
{
  "type": "Mesh",
  "name": "Crate",
  "path": "Generated/Assets/ImportCache/<asset-guid>/Crate.vemesh"
}
```

Dependency records should describe materials, texture paths, shader references, and other assets discovered by import.
Texture dependencies can be metadata-only in the first version.

Import state records describe the local generated cache:

```json
{
  "format": "VEngine.ImportState",
  "version": 1,
  "sourceHash": "content-hash",
  "importer": "ObjModel",
  "importerVersion": 1
}
```

These records are disposable generated files. Reimport may rewrite them and generated artifacts without changing
authored `.veasset` files when GUID, importer settings, artifacts, and dependencies are unchanged.

### 5.2 `.vescene`

`.vescene` is the asset-level scene contract. It wraps the Milestone 5 scene serialization shape with project asset
metadata:

```json
{
  "format": "VEngine.Scene",
  "version": 1,
  "guid": "00000000-0000-0000-0000-000000000000",
  "name": "SampleScene",
  "scene": {
    "gameObjects": []
  }
}
```

Scene component properties should continue to flow through Reflection. Asset references in components, such as
`MeshRendererComponent` mesh and material references, should serialize as asset references rather than runtime
`ResourceId` values:

```json
{
  "guid": "00000000-0000-0000-0000-000000000000",
  "path": "Assets/Samples/Materials/Default.vematerial"
}
```

When a required mesh asset reference cannot be resolved or its `.vemesh` artifact is missing, scene loading should fail
with a clear diagnostic. Material references may warn and fall back to the default material so a scene remains visible
when only material data is missing.

### 5.3 `.vematerial`

`.vematerial` is the first material asset format.

Required first-stage fields:

```json
{
  "format": "VEngine.Material",
  "version": 1,
  "guid": "00000000-0000-0000-0000-000000000000",
  "name": "Default",
  "shader": {
    "guid": "00000000-0000-0000-0000-000000000000",
    "path": "Shaders/Generated/Basic.shader"
  },
  "parameters": {
    "baseColor": [0.8, 0.8, 0.8]
  },
  "textures": []
}
```

The first runtime renderer may only consume `baseColor`. Texture slots should still be represented so source material
import can record texture dependencies without requiring texture decoding to be complete.

### 5.4 `.vemesh`

`.vemesh` is the first VEngine-native mesh payload generated by model import.

The earliest version may remain JSON for debugging:

```json
{
  "format": "VEngine.Mesh",
  "version": 1,
  "sourceGuid": "00000000-0000-0000-0000-000000000000",
  "name": "Crate",
  "vertexFormat": ["position3", "normal3", "color3"],
  "vertices": [
    [-0.5, -0.5, -0.5, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0]
  ],
  "materialSlots": []
}
```

The first writer should pretty-print the JSON, but vertex data should use compact numeric arrays instead of repeating
`position`, `normal`, and `color` object keys for every vertex.

Milestone 5 renders non-indexed mesh data. Milestone 6 expands imported triangles into the existing vertex list. Adding
indexed GPU buffer submission is allowed only if it stays small and local to the render-resource path.

## 6. AssetDatabase Work

The first `AssetDatabase` should provide a simple in-memory index backed by per-asset text files. A central binary
database is not required.

Required responsibilities:

- Open a project root and remember the `Assets/` and `Generated/Assets/` roots.
- Scan authored `.vescene` and `.vematerial` files.
- Scan `.veasset` sidecars for source assets.
- Assign GUIDs when creating new metadata or authored sample assets.
- Detect duplicate GUIDs.
- Map GUID to asset path, asset type, and generated artifacts.
- Map normalized project path to GUID.
- Resolve asset references by GUID with optional path fallback.
- Track importer id, importer version, import settings, generated artifacts, and dependency records in authored
  metadata; track source hash and cache freshness in generated import state.
- Validate missing sources, missing generated artifacts, unknown asset types, and unresolved dependencies.
- Save updated metadata atomically enough for local development.

Recommended first APIs:

```text
AssetDatabase::Open(projectRoot)
AssetDatabase::Refresh()
AssetDatabase::FindAsset(guid)
AssetDatabase::FindAssetByPath(path)
AssetDatabase::CreateSourceAssetMetadata(sourcePath, importerId, settings)
AssetDatabase::CreateAuthoredAsset(path, assetType)
AssetDatabase::UpdateImportResult(guid, artifacts, dependencies)
AssetDatabase::ResolveArtifact(guid, resourceType)
AssetDatabase::Validate()
```

The API names can change during implementation, but the responsibilities should remain explicit and testable.

## 7. Import Pipeline

Import should be deterministic for the same source file, settings, and importer version.

Recommended request shape:

```text
ImportRequest
  projectRoot
  sourcePath
  assetGuid
  importerId
  importerVersion
  settings
  outputRoot
  force
```

Recommended result shape:

```text
ImportResult
  status
  diagnostics
  artifacts
  dependencies
  sourceHash
```

The first static model importer should:

- Accept `obj` through the project-owned OBJ path first.
- Leave the importer registry shape ready for the later assimp backend to add `fbx`, `gltf`, and `glb`.
- Read meshes, normals, vertex colors where available, and material assignments.
- Generate normals only when import settings request it or when the source lacks normals.
- Apply a first explicit unit scale setting.
- Preserve source node or mesh names where available.
- Generate a single combined `.vemesh` artifact for the first renderer path.
- Create or update simple `.vematerial` assets for imported materials when possible.
- Record texture references from source materials as dependencies without requiring full texture import.
- Emit warnings for unsupported channels instead of failing the whole import when the core mesh is usable.

Import output paths should be stable, preferably under a GUID-named folder:

```text
Generated/Assets/ImportCache/<asset-guid>/<artifact-name>.vemesh
```

Stable paths keep scenes and metadata from churning when a source file is reimported.

## 8. Runtime Loading

Runtime loading should start with mesh, material, and scene assets.

Recommended runtime flow:

```text
Scene load request
  -> read .vescene through IOSystem
  -> parse JSON on a worker when useful
  -> resolve asset references through AssetDatabase or a read-only manifest
  -> request mesh/material resources from ResourceManager
  -> apply loaded scene and resource handles on the Game Thread
  -> RenderSystem synchronizes changed resources
```

`ResourceManager` should add explicit file-backed load APIs while keeping built-in fallback resources:

```text
LoadMesh(assetReference)
LoadMaterial(assetReference)
FindOrLoadMesh(assetReference)
FindOrLoadMaterial(assetReference)
```

The exact names may change, but runtime loading should preserve these rules:

- File IO goes through `IOSystem` when the runtime path can be async.
- CPU parsing or conversion can run through `JobSystem`.
- Live `Scene`, `GameObject`, and `Component` mutation happens on the Game Thread.
- Render Thread receives resource data only through render commands or render-resource synchronization.
- Missing mesh assets fail scene loading. Missing material assets warn and resolve to the default material.
- Runtime source model import is not allowed in Player builds.

The first implementation can complete small loads synchronously inside tests and command line tools, as long as the
runtime service API already has a clear async path.

## 9. VEngineAssetTool

`VEngineAssetTool` becomes the command line entry point for import, scan, and validation.

Required first commands:

```text
VEngineAssetTool scan --project <project-root>
VEngineAssetTool import --project <project-root> --source <source-path> [--force]
VEngineAssetTool reimport --project <project-root> --asset <guid-or-path> [--force]
VEngineAssetTool validate --project <project-root>
```

The tool should:

- Initialize logging.
- Open the `AssetDatabase`.
- Use the same importers and metadata writers as the Editor path.
- Return non-zero exit codes for failed import, invalid arguments, or validation failure.
- Print concise diagnostics suitable for CI logs.
- Avoid interactive prompts.

Command line parsing can stay minimal and project-owned until a more complete tool framework is justified.

## 10. Editor-Triggered Import Path

Milestone 6 should add the backend path for Editor-triggered import without building the full Asset Browser.

Acceptable first UI entry points:

- A simple menu command.
- A temporary file path command.
- A small developer-only import panel.

The Editor path should call the same import service as `VEngineAssetTool`. It should not duplicate source-model import
logic or write metadata through separate ad hoc code. Import diagnostics should go through the logging facade so the
Windows debug console and any future tooling hooks receive the same messages.

## 11. Sample Vertical Slice

Milestone 6 should end with a file-backed sample that replaces the Milestone 5 code-only sample scene where practical:

```text
Examples/AssetPipelineSample/.veproject
Examples/AssetPipelineSample/Assets/Samples/Scenes/AssetPipelineSample.vescene
Examples/AssetPipelineSample/Assets/Samples/Materials/Default.vematerial
Examples/AssetPipelineSample/Assets/Samples/Models/<sample-source-model>
Examples/AssetPipelineSample/Assets/Samples/Models/<sample-source-model>.veasset
Examples/AssetPipelineSample/Generated/Assets/ImportCache/<asset-guid>/<sample-mesh>.vemesh
```

The Windows Player should be able to load the sample scene through the asset pipeline path and render it using the
existing camera, light, transform, mesh renderer, ResourceManager, and RenderSystem bridge.

If automated Player window smoke testing is still not stable, keep the verification focused on import, asset lookup,
scene load, resource creation, and render-resource synchronization tests.

## 12. Testing

Prioritize small deterministic tests:

- Asset GUID creation, parsing, formatting, and invalid input handling.
- Project path normalization and path-to-GUID lookup.
- `.veasset` metadata round trip.
- `.vescene` asset-level scene load and save around the Milestone 5 scene serialization shape.
- `.vematerial` load and save.
- Duplicate GUID detection.
- Missing dependency and missing artifact validation.
- AssetDatabase refresh after adding or updating metadata.
- Source-model import smoke test using a tiny checked-in source model.
- Import re-run stability for unchanged source/settings.
- Runtime mesh and material loading into `ResourceManager`.
- Scene load resolving mesh and material asset references.
- `VEngineAssetTool` command line smoke tests for `scan`, `import`, and `validate`.

CTest coverage should stay fast. Large model files should not be introduced for first-stage tests.

## 13. Completion Criteria

Milestone 6 is complete when:

- Asset identity and metadata are stable enough for scene references.
- `AssetDatabase` can scan, resolve, validate, and update first-stage assets.
- `VEngineAssetTool` can import a static model through the first source-model importer into VEngine-native mesh/material
  files.
- `.veasset`, `.vescene`, `.vematerial`, and first `.vemesh` contracts are documented and tested.
- Runtime code can load a `.vescene` that references mesh and material assets by GUID.
- `ResourceManager` can create runtime mesh and material resources from file-backed assets.
- The sample scene renders through the existing Milestone 5 render path.
- Editor-triggered import uses the same backend path as the command line tool.
- Focused CTest coverage exists for metadata, database lookup, import, runtime loading, and tool smoke behavior.
