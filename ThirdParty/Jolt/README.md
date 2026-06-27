# Jolt Physics

VEngine uses Jolt Physics as the project-owned third-party physics backend source. The engine-level Physics facade is not
implemented by this dependency wrapper; this directory only prepares and builds the upstream library.

The pinned upstream version is `5.5.0`, stored as the version-controlled `JoltPhysics-5.5.0.zip` archive.

To prepare source after cloning the repository, extract the bundled archive into `Source/`:

```bat
ThirdParty\Jolt\Build_Windows64.bat
```

To build Jolt's upstream tests and common demos:

```bat
ThirdParty\Jolt\Build_Windows64.bat
```

By default the build script compiles `UnitTests`, `HelloWorld`, `PerformanceTest`, and `Samples` under
`Build/Windows64/<tag>/<configuration>/`. `JoltViewer` is available when the required graphics SDKs are installed:

```bat
ThirdParty\Jolt\Build_Windows64.bat -IncludeViewer
```

Specific targets can be selected with a comma-separated target list:

```bat
ThirdParty\Jolt\Build_Windows64.bat -Targets UnitTests,HelloWorld -Configuration Release
```

The Windows build script is pinned to the same Visual Studio 2022 / MSVC v143 x64 baseline as the VEngine Windows CMake
presets. Downloaded source and standalone build output are ignored by git.
