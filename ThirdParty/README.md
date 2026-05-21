# ThirdParty Dependencies

VEngine keeps third-party dependency setup inside this directory instead of using vcpkg.

After cloning the repository on Windows, prepare third-party dependencies with:

```bat
ThirdParty\Setup_Windows64.bat
```

The script prepares:

- Boost 1.85.0 under `Boost/Build/Windows64`.
- Microsoft DirectXShaderCompiler under `DirectXShaderCompiler/Build/Windows64`.
- SPIRV-Cross under `SPIRV-Cross/Source` and `SPIRV-Cross/Build/Windows64`.

Generated source checkouts, archives, build directories, and binaries are ignored by git. The tracked files in this
directory are the setup scripts, CMake wrappers, and short dependency notes needed to reproduce them.

CMake can still prepare missing shader-tool dependencies during configure/build, but running the setup script first is
recommended for a predictable clone-to-build workflow.
