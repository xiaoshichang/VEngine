# iOS Packaging Design

## 1. Scope

The iOS packaging track follows the same Editor-driven direction as the Windows and macOS package flows, but the build
artifact is an Xcode-signed iOS app archive instead of a hand-assembled desktop bundle.

Current first slice:

- Adds iOS CMake presets and an iOS toolchain file for Xcode generation.
- Adds `VEngineIOSPlayer` as a UIKit application shell with a Metal-backed root view.
- Keeps iOS separate from macOS so iOS builds do not link AppKit or the desktop .NET JIT host.
- Selects the iOS scripting backend through `ScriptingBackendType::IOSAOT` when `ScriptingBackendType::Auto` is used on
  iOS.
- Defines the native side of the iOS scripting boundary as a `ManagedScriptEntryPoints` table supplied by a linked
  .NET NativeAOT payload, with iOS weak-symbol discovery for the standard exported bridge functions.
- Adds iOS third-party setup for Boost static libraries and Jolt source preparation.
- Adds an initial macOS Editor iOS package target that stages runtime data, optionally publishes project scripts through
  .NET NativeAOT, configures an iOS Xcode build tree, and drives `xcodebuild archive` / `xcodebuild -exportArchive`.
- Runs an iOS packaging preflight step before staging files so missing `cmake`, `xcodebuild`, or script-publish `dotnet`
  tooling fails before package directories are mutated.
- Extends the Editor package dialog from a host-only packer to a selectable target packer. Windows exposes Windows
  packaging; macOS exposes macOS and iOS packaging.

Out of scope for this first slice:

- Production-grade signing UI and provisioning profile selection.
- Full asset cooking and platform-specific compression.
- Verified device/simulator build output on a macOS/Xcode host.
- Touch input, engine runtime loop integration, scene loading, and swapchain rendering through the shared Player path.
- Full C# syntax parsing for unusual script declarations; the first registry generator supports normal namespace/class
  declarations used by the current handwritten script style.

## 2. Build Targets

The iOS player target is `VEngineIOSPlayer`.

Recommended presets:

```text
ios-device-debug
ios-device-release
ios-simulator-debug
```

The iOS presets disable Editor, tests, tools, D3D11, and D3D12. They enable Metal and build only the iOS player.

`CMake/VEngineOptions.cmake` performs the same first-line validation for direct CMake users that the macOS Editor packer
performs during package preflight: bundle identifiers must be reverse-DNS compatible, code signing style must be
`Automatic` or `Manual`, manual signing requires `VE_IOS_PROVISIONING_PROFILE_SPECIFIER`, deployment targets must be
numeric dotted versions such as `16.4`, and NativeAOT project/runtime paths must exist when supplied. `CMake/Toolchains/IOS.cmake`
uses `VE_IOS_DEPLOYMENT_TARGET` as the default `CMAKE_OSX_DEPLOYMENT_TARGET`, and the options layer forces the two values
back into sync so Jolt, engine code, Xcode target settings, and NativeAOT publish inputs use the same minimum OS version.

## 3. Platform Split

`APPLE` is not treated as macOS. Platform-specific decisions should branch as:

```text
Windows:
  VE_PLATFORM_WINDOWS=1

iOS:
  VE_PLATFORM_APPLE=1
  VE_PLATFORM_IOS=1
  VE_PLATFORM_MACOS=0

macOS:
  VE_PLATFORM_APPLE=1
  VE_PLATFORM_IOS=0
  VE_PLATFORM_MACOS=1
```

iOS runtime code uses UIKit. macOS runtime code uses AppKit. Shared Apple rendering code may use Foundation, Metal, and
QuartzCore when the APIs are available on both platforms.

## 4. NativeAOT Scripting Direction

iOS does not use the desktop `hostfxr` / JIT scripting backend. The iOS backend expects project scripts to be built into
a .NET NativeAOT static native library and linked into the app by the iOS package build.

The native bridge boundary is:

```text
.NET NativeAOT exports
  -> ManagedScriptEntryPoints
    -> IOSAOTScriptingBackend
      -> ScriptingSystem
```

The desktop/editor script project remains `Library/Scripting/<Project>.Scripts.csproj` and is JIT-only. It references
the bundled `VEngine.ScriptHost.dll` through `VEngineScriptHostAssembly` and does not contain iOS NativeAOT publish
properties, project references, trimmer roots, or generated NativeAOT registry sources.

The iOS package flow generates a separate temporary `<Project>.Scripts.iOS.NativeAOT.csproj` under the package
intermediate output, with the matching `VEngineNativeAOTScriptRegistry.g.cs` under that temporary project's `Generated`
directory. The registry uses direct generic references and a module initializer to call
`NativeScriptBridge.RegisterLinkedScriptType<Project.ScriptType>()` for each discovered script type. This keeps user
script types visible to NativeAOT trimming, preserves public fields and parameterless constructors, and avoids loading
project DLLs at runtime. The generated file also emits the `VEngine_*` `[UnmanagedCallersOnly]` export forwarders inside
the project script assembly, because NativeAOT native-library exports are selected from the published entry assembly.
Those forwarders call public methods on `NativeScriptBridge`; the ScriptHost assembly keeps its own export wrappers for
the case where it is published directly.

The temporary iOS NativeAOT project references `Engine/Managed/VEngine.ScriptHost/VEngine.ScriptHost.csproj` directly,
so ScriptHost is compiled into the NativeAOT static library with the project scripts. The managed payload stays on the
plain `net10.0` target framework and uses the iOS NativeAOT runtime pack selected by the `ios-arm64` runtime identifier
instead of depending on desktop-style `hostfxr` or the iOS workload app model. The ProjectReference is intentionally
copy-local for the AOT publish closure; desktop generated projects keep the ScriptHost DLL reference non-copy-local
because the player/editor already carry the managed host payload. Generated iOS NativeAOT script projects write
`AppleMinOSVersion` from `VE_IOS_DEPLOYMENT_TARGET`, defaulting to `16.4` when no override is available, so project code,
engine code, and the NativeAOT object agree on the same minimum OS version.

The iOS backend first honors an explicit `ScriptingSystemInitParam::nativeAotEntryPoints` table. When that table is not
provided, it attempts to discover the standard `VEngine_*` NativeAOT exports as weak-linked iOS symbols and builds the
entry table automatically. This lets an iOS app start without a script payload, while a packaged app gains scripting when
the NativeAOT static library is linked into `VEngineIOSPlayer`. `LoadProjectAssembly()` is only a metadata
initialization hook for the iOS AOT bridge; it must not perform desktop-style dynamic assembly loading.
The linked bridge is only enabled when all standard exported entry points are present, including lifecycle callbacks and
field serialization setters/getters. A partially linked NativeAOT payload is treated as absent instead of enabling a
runtime bridge with missing editor/runtime script features.

The initial packer publishes generated project scripts with:

```text
dotnet publish <PackageOutput>/NativeAOT/Project/<Project>.Scripts.iOS.NativeAOT.csproj
  --framework net10.0
  --runtime ios-arm64
  --output <PackageOutput>/NativeAOT/Output
```

When a static library is produced, it is passed to the iOS CMake configure step as `VE_IOS_NATIVEAOT_LIBRARY` and linked
into `VEngineIOSPlayer` with `-force_load` so exported bridge symbols are preserved.
The packer also passes `VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR`, normally discovered from the NuGet
`Microsoft.NETCore.App.Runtime.NativeAOT.ios-arm64` package. When multiple package versions are present in the NuGet
cache, the packer compares numeric version segments rather than plain directory names so `10.0.0` is selected ahead of
older versions such as `8.0.22`. The iOS target links the project static library first and then the NativeAOT
runtime/native inputs in the same dependency order used by the .NET NativeAOT Unix targets, including the workstation
GC, disabled eventpipe, native shims, brotli libraries, Apple crypto/security frameworks, Swift runtime libraries, and
system `z`/`icucore`/`objc`/`m` libraries. This is required because `NativeLib=Static` produces a project library with
unresolved NativeAOT runtime symbols; the final app link must resolve them.

The NativeAOT publish step must run on a macOS host for real iOS output. Windows verification can compile the managed
projects and confirm the `ios-arm64` runtime-pack path is selected, but native cross-OS compilation does not produce the
iOS static library there.

The packer prefers NativeAOT libraries whose filename stem matches `<Project>.Scripts` or `lib<Project>.Scripts` before
falling back to the first `.a` under the publish output directory.

After a NativeAOT publish succeeds, the packer writes `Data/Scripts/ScriptAssembly.json` with `assemblyPath` set to
`Scripts/NativeAOT`. This path is a sentinel used by the shared Player script-loading flow to call
`LoadProjectAssembly()`; it is not a desktop managed DLL payload.

The first signing controls are environment variables consumed by the macOS Editor process:

```text
VE_IOS_DEVELOPMENT_TEAM=<Apple Team ID>
VE_IOS_BUNDLE_IDENTIFIER=<explicit app bundle identifier>
VE_IOS_CODE_SIGN_STYLE=Automatic|Manual
VE_IOS_PROVISIONING_PROFILE_SPECIFIER=<profile name or UUID for manual signing>
VE_IOS_CODE_SIGN_IDENTITY=<optional signing identity, such as Apple Development or Apple Distribution>
VE_IOS_DEPLOYMENT_TARGET=16.4
VE_IOS_EXPORT_METHOD=development|ad-hoc|app-store|enterprise
VE_IOS_NATIVEAOT_RUNTIME_NATIVE_DIR=<optional NuGet runtime/native override>
```

`VE_IOS_DEPLOYMENT_TARGET` is passed to the iOS player target settings, `CMAKE_OSX_DEPLOYMENT_TARGET`, and the .NET
NativeAOT `AppleMinOSVersion` property so project code, engine code, and the NativeAOT object agree on the same minimum
OS version; the packer validates that it is a numeric dotted version such as `16.4` before invoking CMake or dotnet.
`VE_IOS_BUNDLE_IDENTIFIER` overrides the generated `com.vengine.packaged.<project>` identifier and is validated during
the preflight step before staging starts. The preflight step also rejects unknown `VE_IOS_CODE_SIGN_STYLE` and
`VE_IOS_EXPORT_METHOD` values before running CMake or Xcode. Manual signing requires
`VE_IOS_PROVISIONING_PROFILE_SPECIFIER`; when provided, the profile is passed to the generated Xcode target, the archive
command, and the export options plist. `VE_IOS_CODE_SIGN_IDENTITY` is optional and is passed through the same archive and
export path when set. When automatic signing is used with a team ID, the iOS packer passes `-allowProvisioningUpdates` to
Xcode archive/export so local Xcode-managed profiles can be refreshed.

## 5. Third-Party Setup

Run iOS dependency setup from a macOS host:

```sh
ThirdParty/Build_IOS.sh
```

The setup entry point validates `VE_IOS_DEPLOYMENT_TARGET`, macOS, and `xcodebuild` before downloading Boost or cleaning
existing dependency outputs, so malformed deployment targets, accidental runs from Windows, or an incomplete Xcode setup
fail before mutating `ThirdParty/Boost/Build/IOS`.

The script prepares:

- Boost 1.85.0 Debug and Release static libraries for `iphoneos` under `ThirdParty/Boost/Build/IOS/device`.
- Boost 1.85.0 Debug and Release static libraries for `iphonesimulator` under `ThirdParty/Boost/Build/IOS/simulator`.
- Jolt source under `ThirdParty/Jolt/Source`.

The Boost iOS script uses `VE_IOS_DEPLOYMENT_TARGET` when it is present, and defaults to `16.4` to match the engine iOS
CMake default. The value must be a numeric dotted version such as `16.4`, matching the macOS Editor packer validation.
Device and simulator outputs remain separate instead of being combined with `lipo`, because current Apple device and
simulator static libraries can both contain `arm64` slices that target different platforms. `CMake/SetupBoostLibrary.cmake`
selects the correct Boost root from `CMAKE_OSX_SYSROOT`. Jolt builds inside the generated VEngine Xcode graph, so it
inherits `CMAKE_OSX_DEPLOYMENT_TARGET` from the iOS packer or preset configure step.

Host tools such as the shader compiler and NativeAOT publish toolchain remain host-side dependencies. They should not be
copied into the iOS app bundle.

## 6. Next Milestones

1. Build `VEngineIOSPlayer` on macOS/Xcode for simulator and device.
2. Verify the Editor iOS packer end-to-end on macOS with a real signing team, provisioning profile, and generated
   NativeAOT script library.
3. Add an iOS `Window` or application surface wrapper that lets `Application` create a UIKit-backed Metal surface.
4. Route touch input into `OSEvent` / `InputSystem`.
5. Load packaged runtime assets and the start scene through the shared Player runtime path inside the iOS app bundle.
