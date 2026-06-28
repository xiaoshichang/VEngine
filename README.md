# VEngine

`VEngine` is a cross-platform lightweight 3D mini game engine built with C++20 and CMake. The first-stage targets are
Windows x64 Player, Windows x64 Editor, Windows tests, and macOS placeholder Player/Editor targets.

This repository is designed around a clone-to-build workflow: project-owned setup scripts prepare third-party
dependencies under `ThirdParty/`, then CMake presets configure and build the engine targets.

## Prerequisites

Windows:

- Visual Studio 2022 Build Tools or Visual Studio 2022 with the Desktop development with C++ workload.
- MSVC v143 x64/x86 build tools installed in the Visual Studio 2022 or Build Tools 2022 instance. The Windows CMake
  presets use the `Visual Studio 17 2022` generator and explicitly request the v143 platform toolset; installing an
  older compiler package inside Visual Studio 2026 is not enough for the documented build path.
- Windows 10/11 SDK installed through Visual Studio Installer.
- CMake 3.25 or newer.
- Git.
- PowerShell.
- Network access for the first third-party dependency setup.

macOS:

- macOS with Xcode.
- CMake 3.25 or newer.

The macOS presets are documented here for completeness, but they cannot be built from Windows.

## Clone And Prepare Dependencies

From a Windows shell:

```bat
git clone <repo-url>
cd VEngine
ThirdParty\Build_Windows64.bat
```

The setup script downloads and builds the project-owned third-party payloads used by the current Windows build:

- Boost.
- DirectXShaderCompiler.
- Slang.
- SPIRV-Cross.

Generated dependency source checkouts, build directories, archives, and binaries are not committed to git. Re-run the
setup script when dependency payloads are missing or after deleting generated third-party directories.

## Configure, Build, And Test On Windows

The checked-in Windows presets are intentionally pinned to Visual Studio 2022/v143. `CMake\Scripts\WithMsvc.bat` only
selects Visual Studio 2022 or Build Tools 2022 installations, and the presets explicitly request `v143`. If the VS 2022
v143 platform toolset is missing, configure fails before any engine targets are generated. Newer Visual Studio versions
need a separate documented preset and third-party dependency baseline.

Run CMake through `CMake\Scripts\WithMsvc.bat` so the MSVC x64 developer environment is initialized. From PowerShell,
prefix the command with `cmd /c`.

Debug build:

```bat
cmd /c CMake\Scripts\WithMsvc.bat cmake --preset windows-msvc-debug
cmd /c CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-debug
```

Release build:

```bat
cmd /c CMake\Scripts\WithMsvc.bat cmake --preset windows-msvc-release
cmd /c CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-release
```

CTest build:

```bat
cmd /c CMake\Scripts\WithMsvc.bat cmake --preset windows-msvc-tests
cmd /c CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests
cmd /c CMake\Scripts\WithMsvc.bat ctest --preset windows-msvc-tests
```

The Windows test target uses CMake/CTest registration. The project does not require a third-party C++ test framework in
the current stage. Unit tests are built only by the dedicated `windows-msvc-tests` preset; the regular Debug and Release
presets build the application and tool targets without test executables.

## Main Windows Targets

The first-stage Windows presets can produce:

- `VEngine.lib`
- `VEnginePlayer.exe`
- `VEngineEditor.exe`
- `VEngineAssetTool.exe`
- `VEngineShaderTool.exe`

Build outputs are generated under `Build/`, for example:

- `Build/windows-msvc-debug/Debug`
- `Build/windows-msvc-release/Release`
- `Build/windows-msvc-tests/Debug`

## macOS Build

On macOS with Xcode:

```sh
cmake --preset mac-debug
cmake --build --preset mac-debug
```

The first-stage macOS path builds `VEngineMacPlayer`, `VEngineMacEditor`, and the Metal triangle demo target when
Metal RHI demos are enabled.

## Clean Regeneration

Project build outputs live under `Build/`. Delete the relevant preset directory, or delete `Build/`, to force a clean
CMake configure and build.

Third-party generated payloads live under each dependency's `ThirdParty/<Dependency>/Build` and source/cache locations.
They can be recreated with:

```bat
ThirdParty\Build_Windows64.bat
```
