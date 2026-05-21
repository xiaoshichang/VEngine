# SPIRV-Cross

VEngine uses SPIRV-Cross for SPIR-V to MSL conversion and shader reflection.

To prepare it explicitly after cloning the repository:

```bat
ThirdParty\SPIRV-Cross\Build_Windows64.bat
```

The script clones the pinned SPIRV-Cross tag into `Source/` and builds the command line tool under
`Build/Windows64/<tag>/<configuration>/`.

CMake uses the same build output path. Downloaded source and standalone build output are ignored by git.
