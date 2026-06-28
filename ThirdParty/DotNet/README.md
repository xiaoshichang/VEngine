# .NET Runtime

VEngine uses this directory for the app-local .NET runtime payload used by the future `nethost` / `hostfxr`
C# scripting backend on Windows and macOS.

The build-time `dotnet` SDK comes from the system installation. This directory only provides the runtime payload
used by hostfxr-based execution.

The runtime version is pinned by `main.py`. Do not pass a version on the command line; changing the runtime version is
a project dependency update and should be reviewed with the same care as other third-party version changes.

To prepare it explicitly after cloning the repository:

```bat
ThirdParty\DotNet\Build_Windows64.bat
```

On macOS, run the shared third-party setup entry point:

```sh
ThirdParty/DotNet/Build_Mac.sh
```

Or:

```sh
python3 ThirdParty/main.py dotnet
```

The script downloads the pinned Microsoft .NET runtime package for the host platform, verifies its SHA512 hash from
the official release metadata, and extracts it to:

```text
ThirdParty/DotNet/win-x64/10.0.9/
ThirdParty/DotNet/osx-arm64/10.0.9/
```

The extracted payload is ignored by git. Packaged Windows players should copy this directory into their app-local
runtime folder instead of depending on a machine-wide .NET installation. macOS scripting can use the same app-local
runtime layout.

If Python reports a certificate verification failure on macOS, install or repair the Python trust store for the local
Python runtime before rerunning the script.
