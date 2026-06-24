# .NET Runtime

VEngine uses this directory for the Windows app-local .NET runtime payload used by the future `nethost` / `hostfxr`
C# scripting backend.

The runtime version is pinned by `Setup_Windows64.ps1`. Do not pass a version on the command line; changing the runtime
version is a project dependency update and should be reviewed with the same care as other third-party version changes.

To prepare it explicitly after cloning the repository:

```bat
ThirdParty\DotNet\Build_Windows64.bat
```

The script downloads the pinned Microsoft .NET runtime zip for `win-x64`, verifies its SHA512 hash from the official
release metadata, and extracts it to:

```text
ThirdParty/DotNet/win-x64/10.0.9/
```

The extracted payload is ignored by git. Packaged Windows players should copy this directory into their app-local
runtime folder instead of depending on a machine-wide .NET installation.
