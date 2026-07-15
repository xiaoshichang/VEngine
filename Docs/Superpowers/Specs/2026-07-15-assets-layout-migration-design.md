# Assets Layout Migration Design

## Goal

Replace the repository-level engine asset layout and its logical content paths:

```text
Assets/BuiltinAsset/Engine/Shaders  -> Assets/Runtime/Shaders
Assets/EditorOnlyAsset/Textures     -> Assets/Editor/Textures
```

The migration is a complete replacement. VEngine will not retain compatibility aliases for `BuiltinAsset` or `EditorOnlyAsset` paths.

## Path Model

Repository-owned runtime assets use the `Runtime/` logical prefix. Repository-owned Editor-only assets use the `Editor/` logical prefix. Project-authored content continues to use the project-relative `Assets/` prefix, and imported content continues to use `Library/`.

The resulting content roots are:

```text
Runtime/   -> <repository>/Assets/Runtime/
Editor/    -> <repository>/Assets/Editor/
Assets/    -> <project>/Assets/
Library/   -> <project>/Library/
```

`ResolveEditorContentPath` and `ToEditorContentPath` will map these logical roots directly to their physical roots. The old `BuiltinAsset/` and `EditorOnlyAsset/` branches and helper names will be removed.

## Asset Database And Import

`EditorAssetDatabase` will scan `<repository>/Assets/Runtime/` as the engine-owned runtime source root. It will not scan `<repository>/Assets/Editor/` into runtime asset metadata.

Shader source metadata and material references will use `Runtime/Shaders/BasicMesh`. Imported shader descriptors and artifacts remain under the project `Library/Imported/` tree. Packaging continues to copy the imported runtime descriptor and every artifact referenced by it.

## Editor Resources

The built-in gizmo icon atlas will move to `Assets/Editor/Textures/BuiltinGizmoIcons.png`. Editor code will resolve it through the `Editor/` logical root. Editor build rules may continue copying the repository `Assets/` tree as long as both new roots preserve their layout in the Editor output.

## Packaging

Player packages include runtime assets recorded by the asset manifest and the imported shader artifacts referenced by those records. Assets under `Assets/Editor/` are not runtime records and therefore are not copied into Player packages.

Documentation and platform packaging examples will use the new `Runtime/` and `Editor/` names. No old path examples or compatibility behavior will remain.

## Validation

Validation will include:

- A repository-wide search proving that functional references to `BuiltinAsset` and `EditorOnlyAsset` have been removed.
- Inspection of the final asset tree and shader metadata.
- Windows configure/build through `CMake/Scripts/WithMsvc.bat` when the configured toolchain is available.

No new unit tests are required for this directory and path migration.
