# DirectXShaderCompiler

VEngine uses `Microsoft.Direct3D.DXC` as the SPIR-V-capable DXC package for shader compilation.

To prepare it explicitly after cloning the repository:

```bat
ThirdParty\DirectXShaderCompiler\Build_Windows64.bat
```

The script downloads and verifies the pinned NuGet package, then copies the DXC tool files to
`Build/Windows64/<version>/Tools/x64`.

CMake also uses the same location when `VE_DXC_EXECUTABLE` is not set. Downloaded packages and generated tool files are
ignored by git under `Build/`.
