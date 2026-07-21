# Mesh Renderer Shadow Controls Design

## Goal

Expose the existing per-object virtual-shadow participation settings in the Editor Inspector so an author can configure whether each `MeshRenderComponent` casts and receives shadows.

## Ownership

`Cast Shadows` and `Receive Shadows` remain properties of `MeshRenderComponent`, not `MaterialResource`.

This keeps shadow participation instance-specific: two objects may share one material while using different shadow settings. Materials continue to define surface and shader behavior. A future material-level shader capability or default may be added separately, but it is outside this first version.

## Inspector Interaction

The expanded `Mesh Renderer` component section displays two checkboxes after the Material field and before Bounds:

- `Cast Shadows`, initialized from `MeshRenderComponent::CastShadows()` and written with `SetCastShadows()` only when edited.
- `Receive Shadows`, initialized from `MeshRenderComponent::ReceiveShadows()` and written with `SetReceiveShadows()` only when edited.

Both settings default to enabled for new or older serialized components. Existing component setters already avoid revision changes for repeated values and already propagate changes to the Render Thread.

## Persistence And Runtime Flow

No new serialization or runtime fields are required. The existing scene serializer persists `castShadows` and `receiveShadows`, and the existing render-item update path carries both settings to `RTRenderItem`.

## Verification

This Editor-bound interaction does not receive a dedicated unit-test executable. Verification consists of:

- Building `VEngineWinEditor` and `VEngineWinPlayer`.
- Running the existing CTest suite unchanged.
- Manually toggling both fields in the Inspector, saving and reopening the scene, and confirming the values persist.

## Non-Goals

- Material-level shadow defaults or overrides.
- Unity-style `Two Sided` or `Shadows Only` casting modes.
- Adding shadow controls to unrelated components.
- Adding an Editor, renderer, or scene-lifecycle unit test.
