# Dear ImGui

VEngine uses Dear ImGui for the Windows Editor only. The source is vendored in this repository under `imgui-1.92.8`.

To verify the expected source files are present:

```bat
ThirdParty\ImGui\Build_Windows64.bat
```

CMake uses `imgui-1.92.8` by default and exposes `VE_IMGUI_SOURCE_DIR` for local override if needed. Do not download or
generate ImGui source during setup; update the vendored directory deliberately when upgrading.
