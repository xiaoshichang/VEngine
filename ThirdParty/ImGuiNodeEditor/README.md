# imgui-node-editor

VEngine vendors imgui-node-editor as an Editor-only dependency under `imgui-node-editor-55a7dbf` and links it against the separately vendored Dear ImGui
1.92.8 target. Dear ImGui itself is unchanged.

The latest official `thedmd/imgui-node-editor` release tag is `v0.9.3`. The vendored snapshot is the exact
[`sonoro1234/imgui-node-editor`](https://github.com/sonoro1234/imgui-node-editor) pull request #335 head commit
`55a7dbf4b517b5e809b372ba39153fe20bad39ad`, based on the official repository's `master` branch while it identified itself as the work-in-progress 0.9.4 version.

This immutable revision combines the two narrow Dear ImGui compatibility changes required by VEngine's 1.92.8 integration:

- [`thedmd/imgui-node-editor#335`](https://github.com/thedmd/imgui-node-editor/pull/335), commit
  `186081d15ae8abab920ce7b0b420e7a4d2cc94c4`, version-gates the duplicate `ImVec2` scalar multiplication operator.
- [`thedmd/imgui-node-editor#339`](https://github.com/thedmd/imgui-node-editor/pull/339), commit
  `ca3d8d2f433ae9e3e3cca7ca609fbde09fbf533d`, version-gates argument ordering at one `PathStroke` and three `AddRect` drawing call sites.

Only the license and library source files required by VEngine are tracked, including `imgui_node_editor_api.cpp`, which defines the public API declared by
`imgui_node_editor.h`. The validator checks the SHA-256 content hash of every tracked upstream file. To verify
the vendored payload:

```bat
ThirdParty\ImGuiNodeEditor\Build_Windows64.bat
```

```sh
ThirdParty/ImGuiNodeEditor/Build_Mac.sh
```

CMake derives the default source directory from `VE_IMGUI_NODE_EDITOR_REVISION` on every configure and enforces the same content hashes for that repository-vendored
snapshot. Set `VE_IMGUI_NODE_EDITOR_SOURCE_DIR` only for an explicit development source-tree override; overrides must contain every required file but are not tied to
the pinned upstream hashes.
