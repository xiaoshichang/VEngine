# Dear ImGui

VEngine uses Dear ImGui for the Windows Editor only. Runtime targets and players should not depend on this wrapper.

To prepare it explicitly after cloning the repository:

```bat
ThirdParty\ImGui\Build_Windows64.bat
```

The script clones the pinned Dear ImGui tag to `Source`. CMake uses the same location when
`VE_IMGUI_SOURCE_DIR` is not set and `VE_IMGUI_DOWNLOAD_IF_MISSING` is enabled.

Generated source checkouts are ignored by git under `Source/`.
