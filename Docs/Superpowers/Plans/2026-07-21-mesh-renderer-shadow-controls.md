# Mesh Renderer Shadow Controls Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expose the existing per-object cast-shadow and receive-shadow settings in the Mesh Renderer Inspector.

**Architecture:** Keep both settings owned by `MeshRenderComponent`. The Inspector reads their current values and calls the existing setters only after an ImGui checkbox edit; existing serialization and Render Thread propagation remain unchanged.

**Tech Stack:** C++20, Dear ImGui, VEngine Editor Inspector, CMake/MSVC, CTest.

---

## File Structure

- Modify `Editor/Panels/InspectorPanel/InspectorComponentEditors.cpp`: render and apply the two Mesh Renderer shadow checkboxes.
- No runtime, material, serialization, RHI, or test source changes are required.

### Task 1: Add Mesh Renderer shadow controls

**Files:**

- Modify: `Editor/Panels/InspectorPanel/InspectorComponentEditors.cpp:280`

- [ ] **Step 1: Confirm the missing controls and existing runtime contract**

Inspect `InspectorPanel::RenderMeshRenderComponent`, `MeshRenderComponent::CastShadows`, `SetCastShadows`, `ReceiveShadows`, and `SetReceiveShadows`. Confirm the Inspector currently renders Mesh, Material, and Bounds but no shadow fields, while the component and serializer already contain both properties.

- [ ] **Step 2: Add the two Inspector fields**

Immediately after the Material field and before Bounds, add:

```cpp
bool castShadows = mesh.CastShadows();
if (RenderFieldCheckbox("Cast Shadows", &castShadows))
{
    mesh.SetCastShadows(castShadows);
}

bool receiveShadows = mesh.ReceiveShadows();
if (RenderFieldCheckbox("Receive Shadows", &receiveShadows))
{
    mesh.SetReceiveShadows(receiveShadows);
}
```

Do not add material-level overrides, new component fields, or serialization changes.

- [ ] **Step 3: Format and inspect the focused diff**

Run the repository's configured clang-format on `Editor/Panels/InspectorPanel/InspectorComponentEditors.cpp`, then run:

```text
git diff --check
git diff -- Editor/Panels/InspectorPanel/InspectorComponentEditors.cpp
```

Expected: only the two checkbox blocks appear in the production-code diff, with no whitespace errors.

- [ ] **Step 4: Build product targets and run existing tests**

Run through the normalized child environment and `CMake/Scripts/WithMsvc.bat`:

```text
cmake --build --preset windows-msvc-debug --target VEngineWinEditor VEngineWinPlayer
cmake --build --preset windows-msvc-tests
ctest --preset windows-msvc-tests --output-on-failure
```

Expected: Editor and Player link successfully, and all existing tests pass. Do not register a dedicated Inspector, scene-lifecycle, or renderer unit test.

- [ ] **Step 5: Perform the Editor acceptance check**

In the Editor, select a GameObject with a Mesh Renderer and verify:

1. Both checkboxes appear between Material and Bounds.
2. Each checkbox can be changed independently.
3. Saving and reopening the scene preserves both values.
4. Two objects sharing one material can retain different checkbox values.

- [ ] **Step 6: Commit the implementation**

```text
git add Editor/Panels/InspectorPanel/InspectorComponentEditors.cpp
git commit -m "editor: expose mesh shadow controls"
```
