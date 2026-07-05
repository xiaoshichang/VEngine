# DirectXShaderCompiler

VEngine uses `Microsoft.Direct3D.DXC` as the DXIL-capable DXC package for shader compilation.

The package version and hash are defined by `CMake/ThirdParty/SetupDirectXShaderCompiler.cmake` and passed to this setup
script by CMake configure. To prepare it explicitly, pass the same values:

```bat
ThirdParty\DirectXShaderCompiler\Build_Windows64.bat --version <version> --sha256 <sha256>
```

The script downloads and verifies the pinned NuGet package, then copies the DXC tool files to
`Build/Windows64/<version>/Tools/x64`.

CMake also uses the same location when `VE_DXC_EXECUTABLE` is not set. Downloaded packages and generated tool files are
ignored by git under `Build/`.
