# VEngine

`VEngine` is a cross-platform lightweight 3D mini game engine built with C++20 and CMake. The first-stage targets are
Windows x64 Player, Windows x64 Editor, Windows tests, and an iOS Simulator Player path.

This repository is designed around a clone-to-build workflow: project-owned setup scripts prepare third-party
dependencies under `ThirdParty/`, then CMake presets configure and build the engine targets.

## Prerequisites

Windows:

- Visual Studio 2022 with the Desktop development with C++ workload.
- Windows 10/11 SDK installed through Visual Studio Installer.
- CMake 3.25 or newer.
- Git.
- PowerShell.
- Network access for the first third-party dependency setup.

iOS:

- macOS with Xcode.
- CMake 3.25 or newer.

The iOS presets are documented here for completeness, but they cannot be built from Windows.

## Clone And Prepare Dependencies

From a Windows shell:

```bat
git clone <repo-url>
cd VEngine
ThirdParty\Setup_Windows64.bat
```

The setup script downloads and builds the project-owned third-party payloads used by the current Windows build:

- Boost.
- DirectXShaderCompiler.
- SPIRV-Cross.

Generated dependency source checkouts, build directories, archives, and binaries are not committed to git. Re-run the
setup script when dependency payloads are missing or after deleting generated third-party directories.

## Configure, Build, And Test On Windows

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
the current stage.

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

## iOS Simulator Build

On macOS with Xcode:

```sh
cmake --preset ios-simulator-debug
cmake --build --preset ios-simulator-debug
```

The first-stage iOS path builds `VEngineIOSPlayer` and the Metal triangle demo target when Metal RHI demos are enabled.

## Clean Regeneration

Project build outputs live under `Build/`. Delete the relevant preset directory, or delete `Build/`, to force a clean
CMake configure and build.

Third-party generated payloads live under each dependency's `ThirdParty/<Dependency>/Build` and source/cache locations.
They can be recreated with:

```bat
ThirdParty\Setup_Windows64.bat
```

