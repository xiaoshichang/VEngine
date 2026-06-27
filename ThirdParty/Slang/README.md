# Slang

VEngine uses Slang for HLSL to SPIR-V generation in the offline shader toolchain.

The Windows payload is expected at:

```text
ThirdParty/Slang/windows64/bin/slangc.exe
```

To validate it explicitly after cloning the repository:

```bat
ThirdParty\Slang\Setup_Windows64.bat
```

`VEngineShaderTool` still uses DXC for D3D12 DXIL and SPIRV-Cross for MSL generation and raw SPIR-V reflection.
