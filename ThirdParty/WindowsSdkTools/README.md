# Windows SDK Tools

VEngine keeps `fxc.exe` here as a builtin shader compilation tool for the D3D11 bytecode path.

The editor and asset importer use:

```text
ThirdParty/WindowsSdkTools/Tools/x64/fxc.exe
```

Keeping this tool in the repository avoids depending on a globally installed Windows SDK or on the `PATH` inherited by a
GUI-launched editor process.

To refresh the local copy from an installed Windows 10/11 SDK:

```bat
ThirdParty\WindowsSdkTools\Build_Windows64.bat
```

The setup script verifies the expected SHA-256 hash after copying the pinned tool version.
