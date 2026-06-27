# SPIRV-Cross

VEngine uses SPIRV-Cross for SPIR-V to MSL conversion and shader reflection.

To prepare it explicitly after cloning the repository:

```bat
ThirdParty\SPIRV-Cross\Build_Windows64.bat
```

The Windows build script is intentionally pinned to Visual Studio 2022 Build Tools with the MSVC v143 x64/x86 toolset.
It uses CMake's `Visual Studio 17 2022` generator and `-T v143`; newer Visual Studio toolsets are not selected by this
script.

The script expands the bundled source archive into `Source/` and builds the command line tool under
`Build/Windows64/<tag>/<configuration>/`.

CMake uses the same build output path. Downloaded source and standalone build output are ignored by git.
