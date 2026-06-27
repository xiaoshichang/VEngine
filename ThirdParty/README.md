# ThirdParty Dependencies

VEngine keeps third-party dependency setup inside this directory instead of using vcpkg.

Windows dependency setup uses the same compiler baseline as the main Windows presets: Visual Studio 2022 Build Tools or
Visual Studio 2022 with the MSVC v143 x64/x86 build tools installed. A machine with only Visual Studio 2026/v180 can
initialize an MSVC environment, but it is not accepted by the documented setup path. The `windows-msvc-*` presets and
SPIRV-Cross standalone build path are pinned to the `Visual Studio 17 2022` generator, `-T v143`, and v143-compatible
Boost artifacts.

After cloning the repository on Windows, prepare third-party dependencies with:

```bat
ThirdParty\Build_Windows64.bat
```

The script prepares:

- Boost 1.85.0 under `Boost/Build/Windows64`.
- Microsoft DirectXShaderCompiler under `DirectXShaderCompiler/Build/Windows64`.
- Slang under `Slang/slang-2026.12-windows-x86_64`.
- Microsoft .NET Runtime 10.0.9 under `DotNet/win-x64/10.0.9`.
- Windows SDK `fxc.exe` under `WindowsSdkTools/Tools/x64`.
- SPIRV-Cross under `SPIRV-Cross/Source` and `SPIRV-Cross/Build/Windows64`.
- Dear ImGui is vendored under `ImGui/imgui-1.92.8`.
- Jolt Physics source under `Jolt/Source`.

Generated source checkouts, archives, build directories, and binaries are ignored by git. Vendored source libraries and
small builtin tools such as Dear ImGui and `WindowsSdkTools/Tools/x64/fxc.exe` are tracked in this directory with their
CMake wrappers and short dependency notes.

CMake can still prepare missing shader-tool dependencies during configure/build, but running the setup script first is
recommended for a predictable clone-to-build workflow.

The .NET runtime payload is app-local infrastructure for the future Windows C# scripting host. Its version is pinned in
`DotNet/main.py` and is not selected through command line arguments.
