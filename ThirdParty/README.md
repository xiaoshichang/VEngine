# ThirdParty Dependencies

VEngine keeps third-party dependency setup inside this directory instead of using vcpkg.

Windows dependency setup uses the same compiler baseline as the main Windows presets: Visual Studio 2022 Build Tools or
Visual Studio 2022 with the MSVC v143 x64/x86 build tools installed. A machine with only Visual Studio 2026/v180 can
initialize an MSVC environment, but it is not accepted by the documented setup path. The `windows-msvc-*` presets are
pinned to the `Visual Studio 17 2022` generator, `-T v143`, and v143-compatible Boost artifacts.

After cloning the repository on Windows, dependencies can still be prepared explicitly with:

```bat
ThirdParty\Build_Windows64.bat
```

The script prepares:

- Boost 1.85.0 under `Boost/Build/Windows64`.
- Microsoft DirectXShaderCompiler under `DirectXShaderCompiler/Build/Windows64`.
- Slang under `Slang/slang-2026.12-windows-x86_64`.
- Microsoft .NET Runtime 10.0.9 under `DotNet/win-x64/10.0.9` and `DotNet/osx-arm64/10.0.9`.
- Windows SDK `fxc.exe` under `WindowsSdkTools/Tools/x64`.
- Dear ImGui is vendored under `ImGui/imgui-1.92.8`.
- Jolt Physics source under `Jolt/Source`.

Generated source checkouts, archives, build directories, and binaries are ignored by git. Vendored source libraries and
small builtin tools such as Dear ImGui and `WindowsSdkTools/Tools/x64/fxc.exe` are tracked in this directory with their
CMake wrappers and short dependency notes.

CMake configure prepares missing project-owned dependency payloads on demand. Each CMake wrapper target checks the files
it needs and invokes `ThirdParty/main.py` for that dependency when the payload is absent, so a normal configure no longer
depends on a manual setup step. The setup scripts remain useful when you want to pre-warm the dependency cache or diagnose
a dependency in isolation.

The .NET runtime payload is app-local infrastructure for the future Windows C# scripting host. Its version is pinned in
`DotNet/main.py` and is not selected through command line arguments.

On macOS hosts that will build iOS players, the iOS dependency payloads are prepared by CMake configure when missing. You
can also prepare them explicitly with:

```sh
ThirdParty/Build_IOS.sh
```

The iOS setup entry point fails before downloading or cleaning dependency directories when it is not running on macOS or
when `xcodebuild` is not available. Install Xcode and select it with `xcode-select` before running the script.

The iOS setup path builds Boost 1.85.0 static libraries for `iphoneos` and `iphonesimulator`, including Debug and
Release variants, and extracts the Jolt Physics source tree used by the embedded CMake target. Boost uses
`VE_IOS_DEPLOYMENT_TARGET` only when it is explicitly set. Otherwise setup detects the installed iPhoneOS SDK version
with Xcode, writes it to `ThirdParty/IOSSettings.json`, and uses that value consistently for Boost and generated iOS
CMake projects. Device and simulator Boost outputs stay split under `Boost/Build/IOS/device` and
`Boost/Build/IOS/simulator`; they are not merged with `lipo` because modern Apple device and simulator libraries may
both contain `arm64` slices for different platforms. Jolt is not prebuilt by `Build_IOS.sh`; it is compiled inside the
iOS Xcode project so it inherits the app SDK, architecture, and `CMAKE_OSX_DEPLOYMENT_TARGET`. Host-side tools such as
Slang and the .NET SDK stay host tools; they are not copied into the iOS app bundle.
