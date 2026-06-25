# HLSL To Metal Shader Flow Feasibility

## 1. Purpose

This document expands `Docs/DevelopmentPlan.md` section `2.2 HLSL To Metal Shader Flow`.

The goal is to define a practical shader pipeline that lets VEngine author shaders once in HLSL, compile them for
D3D11, D3D12, and Metal, and keep runtime shader loading simple enough for the first rendering milestones.

For the current command-line usage guide and examples, see `Docs/ShaderToolUsage.md`.

## 2. Decision Summary

The recommended first-stage pipeline is:

```text
HLSL source
  -> VEngineShaderTool
    -> D3D11: DXBC through D3DCompile / fxc-compatible path
    -> D3D12: DXIL through DirectXShaderCompiler
    -> Metal:
      -> SPIR-V through DirectXShaderCompiler
      -> MSL source through SPIRV-Cross
      -> metallib through Apple's metal command line tools during Apple-platform packaging
    -> Normalized reflection metadata
```

Use `DirectXShaderCompiler` and `SPIRV-Cross` in the offline toolchain, not in the normal Player runtime.

Use Apple Metal Shader Converter as a future optional path for DXIL-to-Metal library generation, but do not make it
the first-stage path. It is valuable for high-end DirectX 12 feature parity, but it raises the minimum runtime and
binding-model expectations compared with the current iOS Simulator milestone.

## 3. Feasibility Result

The flow is feasible for the first rendering slice:

- Vertex and pixel shaders.
- Constant buffers.
- Texture2D resources.
- Sampler states.
- Basic material parameters.
- Static mesh rendering.
- D3D11, D3D12, and iOS Metal triangle and mesh smoke tests.

The flow is not a complete promise that every HLSL feature will cross-compile cleanly. VEngine should define a small
portable HLSL subset first, validate generated MSL with Apple's compiler, and grow feature support only when backed by
tests.

The most important constraint is D3D11. D3D12 and Metal can share a more modern shader model through DXC and SPIR-V,
but D3D11 should use a Shader Model 5 DXBC path. First-stage shared shaders therefore need to compile under both the
D3D11 path and the D3D12/Metal path.

## 4. Tooling Assessment

### 4.1 DirectXShaderCompiler

`DirectXShaderCompiler` is the primary compiler for modern HLSL. It compiles HLSL to DXIL for D3D12 and can also target
SPIR-V through its SPIR-V code generation path.

Use it for:

- D3D12 shader bytecode.
- HLSL syntax validation for modern profiles.
- HLSL to SPIR-V generation for the Metal cross-compile path.

Do not use DXIL directly for D3D11.

### 4.2 D3DCompile / FXC-Compatible Path

D3D11 should receive DXBC bytecode. Use the Windows `D3DCompile` API or an equivalent `fxc` command-line path from
`VEngineShaderTool`.

This gives a clean split:

- D3D11: `vs_5_0`, `ps_5_0`, DXBC.
- D3D12: `vs_6_0+`, `ps_6_0+`, DXIL.
- Metal: HLSL profile accepted by DXC SPIR-V generation, then MSL.

### 4.3 SPIRV-Cross

`SPIRV-Cross` is the first-stage bridge from SPIR-V to Metal Shading Language. It also provides reflection data that
can be normalized into VEngine's shader metadata.

Use it for:

- MSL source generation.
- Reflection over resources, stage inputs, stage outputs, and constant buffers.
- Optional binding remapping before MSL emission.

### 4.4 Apple Metal Tools

For iOS and other Apple targets, generated MSL should be compiled into a Metal library during Apple-platform packaging.

Recommended packaging path:

```text
Generated .metal files
  -> xcrun -sdk iphonesimulator metal
  -> .air or .metallib
  -> bundled into iOS app
  -> loaded by MetalRHI through MTLLibrary
```

The engine runtime should load a precompiled `.metallib` or default bundle library for normal execution. Runtime
compilation from MSL source should be treated as a debug-only fallback.

### 4.5 Apple Metal Shader Converter

Apple Metal Shader Converter converts DXIL or other LLVM IR bytecode into Metal-compatible bytecode and can produce
`.metallib` output. It is available as a standalone executable and as a library, and it supports both Windows and macOS.

It should be recorded as a future option, not the first-stage default, because:

- Its Metal libraries require Argument Buffers Tier 2 for full support.
- The documented runtime baseline is macOS 14 / iOS 17 or later for full support.
- The first iOS target is an early simulator demo, and the current engine architecture plans direct-slot Metal binding
  before argument buffers.
- The SPIR-V to MSL path produces inspectable MSL text, which is easier to debug while the RHI and shader metadata are
  still being designed.

Re-evaluate Metal Shader Converter when VEngine moves beyond the initial forward-rendering slice or raises the Apple
runtime baseline.

## 5. Recommended Pipeline

### 5.1 Source Inputs

Use HLSL as the only authored runtime shader language.

Recommended source layout:

```text
Shaders/
  HLSL/
    Common/
      Packing.hlsli
      Lighting.hlsli
    BasicMesh.hlsl
  Generated/
    DXBC/
    DXIL/
    SPIRV/
    MSL/
    MetalLib/
    Reflection/
```

Each shader should have a small text manifest that declares stages, entry points, target platforms, feature defines,
and variant keys.

Example:

```json
{
    "name": "BasicMesh",
    "source": "Shaders/HLSL/BasicMesh.hlsl",
    "stages": [
        {
            "stage": "Vertex",
            "entry": "VSMain"
        },
        {
            "stage": "Pixel",
            "entry": "PSMain"
        }
    ],
    "variants": [
        {
            "name": "Default",
            "defines": []
        }
    ]
}
```

### 5.2 D3D12 Output

Compile with DXC to DXIL.

Command shape:

```text
dxc -T vs_6_0 -E VSMain -Fo BasicMesh.VS.dxil BasicMesh.hlsl
dxc -T ps_6_0 -E PSMain -Fo BasicMesh.PS.dxil BasicMesh.hlsl
```

Later milestones may move to newer shader model profiles, but the first stage should use the lowest SM6 profile that
passes the engine's feature needs.

### 5.3 D3D11 Output

Compile with `D3DCompile` or `fxc` to DXBC.

Command shape:

```text
fxc /T vs_5_0 /E VSMain /Fo BasicMesh.VS.dxbc BasicMesh.hlsl
fxc /T ps_5_0 /E PSMain /Fo BasicMesh.PS.dxbc BasicMesh.hlsl
```

If `VEngineShaderTool` uses the API path instead of shelling out, it should call `D3DCompile` with equivalent target
profiles and flags.

### 5.4 SPIR-V Output For Metal

Compile HLSL with DXC to SPIR-V.

Command shape:

```text
dxc -spirv -T vs_6_0 -E VSMain -fvk-use-dx-layout -Fo BasicMesh.VS.spv BasicMesh.hlsl
dxc -spirv -T ps_6_0 -E PSMain -fvk-use-dx-layout -Fo BasicMesh.PS.spv BasicMesh.hlsl
```

The exact SPIR-V target environment should be a `VEngineShaderTool` option, not hard-coded in shader source. Start with
a conservative Vulkan environment supported by DXC and SPIRV-Cross, then lock the value once the first iOS Metal smoke
test is in place.

Use `-fvk-use-dx-layout` initially to reduce layout drift from the DirectX path. Reflection metadata and explicit
packing tests remain mandatory; do not rely only on compiler flags for CPU/GPU layout compatibility.

### 5.5 MSL Output

Convert SPIR-V to MSL with SPIRV-Cross.

Command shape:

```text
spirv-cross BasicMesh.VS.spv --msl --msl-ios --output BasicMesh.VS.metal
spirv-cross BasicMesh.PS.spv --msl --msl-ios --output BasicMesh.PS.metal
```

For the C++ API integration, configure the same options programmatically so command-line and library behavior remain
equivalent.

### 5.6 Metal Library Output

Compile generated MSL to a Metal library as part of the Apple build or asset-cook step.

Command shape:

```text
xcrun -sdk iphonesimulator metal -c BasicMesh.VS.metal -o BasicMesh.VS.air
xcrun -sdk iphonesimulator metal -c BasicMesh.PS.metal -o BasicMesh.PS.air
xcrun -sdk iphonesimulator metallib BasicMesh.VS.air BasicMesh.PS.air -o BasicMesh.metallib
```

The SDK should match the target:

- `iphonesimulator` for iOS Simulator.
- `iphoneos` for iOS device builds.
- `macosx` only for future macOS tooling or host validation.

## 6. Binding Model

### 6.1 Authoring Rules

All bindable resources must use explicit HLSL registers and spaces.

Example:

```hlsl
cbuffer CameraConstants : register(b0, space0)
{
    row_major float4x4 viewProjection;
};

Texture2D BaseColorTexture : register(t0, space0);
SamplerState BaseColorSampler : register(s0, space0);
```

Do not rely on compiler auto-binding in first-stage shaders.

### 6.2 Canonical VEngine Binding Key

Normalize every shader resource to this key:

```text
ShaderStage
BindGroup
ResourceKind
BindingIndex
ResourceName
```

Mapping:

```text
HLSL spaceN              -> BindGroup N
HLSL bN/tN/sN/uN         -> BindingIndex N within the resource kind
D3D11                   -> Stage slot N
D3D12                   -> Descriptor range register N, space N
Metal direct-slot path   -> buffer/texture/sampler index assigned from reflection mapping
```

The first Metal backend should use direct resource slots. Argument buffers should be introduced later only when the RHI
and material system need them.

### 6.3 Metal Slot Allocation

Metal has separate buffer, texture, and sampler index spaces. VEngine should generate deterministic Metal slot mapping
from reflection instead of assuming that HLSL register numbers are always valid Metal indices.

First-stage policy:

- Constant buffers map to Metal buffer indices.
- Structured buffers map to Metal buffer indices.
- Textures map to Metal texture indices.
- Samplers map to Metal sampler indices.
- The mapping is stored in reflection metadata and used by `MetalRHI` when encoding draw commands.

### 6.4 Bind Group Strategy

Initial bind groups:

```text
space0 -> Per frame / camera / scene
space1 -> Per material
space2 -> Per draw / object
space3 -> Reserved for future global resources
```

D3D11 can collapse this into direct stage slots internally. The public RHI should keep the bind group concept so D3D12
and Metal remain natural.

## 7. Reflection Metadata

`VEngineShaderTool` should emit one normalized reflection file per shader variant.

Recommended output:

```text
Shaders/Generated/Reflection/BasicMesh.Default.veshader.json
```

Minimal schema:

```json
{
    "schemaVersion": 1,
    "name": "BasicMesh",
    "variant": "Default",
    "sourceHash": "hash",
    "stages": [
        {
            "stage": "Vertex",
            "entry": "VSMain",
            "artifacts": {
                "d3d11": "Shaders/Generated/DXBC/BasicMesh.Default.VS.dxbc",
                "d3d12": "Shaders/Generated/DXIL/BasicMesh.Default.VS.dxil",
                "metal": "Shaders/Generated/MetalLib/BasicMesh.Default.metallib"
            },
            "resources": [
                {
                    "name": "CameraConstants",
                    "kind": "ConstantBuffer",
                    "bindGroup": 0,
                    "binding": 0,
                    "hlslRegister": "b0",
                    "hlslSpace": 0,
                    "metalIndex": 0,
                    "sizeBytes": 64
                }
            ],
            "vertexInputs": [
                {
                    "semantic": "POSITION",
                    "semanticIndex": 0,
                    "format": "Float3",
                    "location": 0
                }
            ]
        }
    ]
}
```

Reflection is the contract between shader compilation, material loading, pipeline creation, and RHI binding.

## 8. Runtime Loading

### 8.1 D3D11

Runtime loads DXBC blobs and creates native D3D11 shader objects:

```text
DXBC blob -> ID3D11Device::CreateVertexShader / CreatePixelShader
```

Input layout should be created from the RHI pipeline descriptor plus reflected vertex input semantics.

### 8.2 D3D12

Runtime loads DXIL blobs and places them into pipeline state descriptors:

```text
DXIL blob -> D3D12_SHADER_BYTECODE -> ID3D12PipelineState
```

Root signature compatibility should be verified against normalized reflection.

### 8.3 Metal

Runtime loads a `.metallib` from the app bundle and retrieves functions by generated stable names.

```text
metallib -> MTLLibrary -> MTLFunction -> MTLRenderPipelineState
```

Generated function names must be recorded in reflection metadata because SPIRV-Cross may transform entry point names.

## 9. Portable HLSL Subset

The first-stage shader subset should be intentionally small.

Allowed:

- Vertex and pixel shader stages.
- `cbuffer`.
- `Texture2D`.
- `SamplerState`.
- Basic arithmetic.
- Matrix/vector math.
- Static branches controlled by compile-time defines.
- Explicit registers and spaces.
- Standard semantics such as `POSITION`, `NORMAL`, `TEXCOORD`, `SV_Position`, and `SV_Target`.

Avoid in the first stage:

- Shader Model 6 features that cannot compile through the D3D11 path.
- Wave operations.
- Mesh shaders.
- Ray tracing shaders.
- Geometry and tessellation shaders.
- Compute shaders.
- Bindless resource arrays.
- Descriptor heap indexing.
- UAV-heavy workflows.
- Globally coherent memory behavior.
- Platform-specific intrinsics.

Rules:

- Use explicit padding in CPU-shared constant structures.
- Treat reflection as the source of truth for buffer sizes and offsets.
- Avoid `bool` fields in constant buffers.
- Use project-standard matrix packing. The initial recommendation is `row_major` in HLSL because it is easy to align
  with typical C++ math storage, but this must be validated once the math library lands.
- Keep include paths deterministic and rooted under `Shaders/HLSL`.
- Every shader that is part of the first-stage runtime must pass D3D11, D3D12, SPIR-V, MSL, and reflection generation.

## 10. VEngineShaderTool Responsibilities

`VEngineShaderTool` should own shader compilation and packaging.

Responsibilities:

- Parse shader manifests.
- Expand variants.
- Resolve includes.
- Invoke or embed DXC.
- Invoke `D3DCompile` for D3D11 output on Windows.
- Invoke or embed SPIRV-Cross for MSL and reflection.
- Emit normalized reflection metadata.
- Validate required resource bindings.
- Validate generated MSL on Apple hosts.
- Produce deterministic output paths.
- Report diagnostics in a format usable by the Editor.

The runtime engine library should not depend on DXC or SPIRV-Cross.

## 11. Build And CMake Integration

Recommended CMake shape:

```text
CMake/
  SetupDirectXShaderCompiler.cmake
  SetupSpirvCross.cmake

Tools/
  ShaderTool/

Shaders/
  HLSL/
  Generated/
```

CMake options:

```cmake
option(VE_BUILD_SHADER_TOOL "Build VEngineShaderTool" ON)
option(VE_SHADER_COMPILE_TESTS "Build shader compile smoke tests" ON)
set(VE_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/Generated/Shaders" CACHE PATH "Generated shader output directory")
```

Host behavior:

- Windows host: build and run D3D11, D3D12, SPIR-V, MSL text generation, and reflection tests.
- Apple host: additionally compile generated MSL to `.metallib` for iOS Simulator and validate Metal pipeline creation.
- Non-Apple host: do not require `xcrun metal`; produce MSL text and defer `.metallib` packaging.

## 12. Smoke Tests

Minimum shader tests:

1. Compile `BuiltinAsset/Engine/Shaders/BasicMesh.hlsl` to D3D11 DXBC.
2. Compile `BuiltinAsset/Engine/Shaders/BasicMesh.hlsl` to D3D12 DXIL.
3. Compile `BuiltinAsset/Engine/Shaders/BasicMesh.hlsl` to SPIR-V.
4. Convert SPIR-V to MSL.
5. Generate reflection JSON.
6. Validate that `CameraConstants : register(b0, space0)` maps to the expected binding metadata.
7. On Apple host, compile generated MSL to `.metallib`.
8. On iOS Simulator, create `MTLLibrary`, retrieve vertex and fragment functions, and render a triangle.

Runtime smoke tests:

- D3D11 triangle demo uses shader artifacts generated from HLSL.
- D3D12 triangle demo uses shader artifacts generated from the same HLSL.
- iOS Metal triangle demo uses MSL/metallib generated from the same HLSL.

Negative tests:

- Missing explicit register should fail.
- Unsupported stage should fail.
- Shader that passes D3D12 but fails D3D11 should be marked incompatible with the D3D11 target.
- Reflection mismatch between stages should fail pipeline creation.

## 13. Implementation Roadmap

### Phase 1: Tool Skeleton

- Add shader manifest format.
- Add `VEngineShaderTool compile` command.
- Add deterministic output directory handling.
- Add diagnostics model.

### Phase 2: D3D Outputs

- Add D3D11 DXBC generation through `D3DCompile`.
- Add D3D12 DXIL generation through DXC.
- Add Windows shader compile smoke tests.

### Phase 3: SPIR-V, MSL, And Reflection

- Add DXC SPIR-V generation.
- Add SPIRV-Cross MSL generation.
- Add SPIRV-Cross reflection extraction.
- Normalize metadata into `.veshader.json`.

### Phase 4: Metal Packaging

- Add Apple-host packaging command that compiles generated MSL to `.metallib`.
- Bundle `.metallib` into the iOS demo target.
- Load Metal functions through `MetalRHI`.

### Phase 5: RHI Integration

- Add `RhiShaderModule` artifact loading.
- Add `RhiPipelineState` creation from generated artifacts.
- Add bind group mapping from reflection metadata.
- Replace hard-coded demo shaders with generated shader artifacts.

### Phase 6: Future Evaluation

- Evaluate Apple Metal Shader Converter for DXIL-to-Metal output.
- Evaluate Metal argument buffers.
- Add compute shader support.
- Add pipeline cache and Metal binary archive support.
- Add editor-visible shader diagnostics and hot reload.

## 14. Risks And Mitigations

### 14.1 D3D11 And D3D12 Profile Divergence

Risk:

The same HLSL source may compile for D3D12 but fail for D3D11.

Mitigation:

- First-stage runtime shaders must compile for all enabled runtime targets.
- Shader manifests can later allow target-specific variants.
- Keep the first feature set within Shader Model 5 compatible language rules.

### 14.2 Binding Layout Mismatch

Risk:

HLSL registers, D3D12 descriptor ranges, and Metal resource indices do not naturally mean the same thing.

Mitigation:

- Require explicit HLSL registers.
- Generate a normalized reflection file.
- Make native backends consume reflection metadata instead of guessing.
- Add binding smoke tests for every supported resource kind.

### 14.3 Constant Buffer Packing Mismatch

Risk:

CPU-side structs and generated shader structs can diverge, especially around matrices, vectors, and padding.

Mitigation:

- Generate or validate layout metadata.
- Avoid fragile field types in constant buffers.
- Use explicit padding in shared structs.
- Add tests that verify byte offsets for common constant buffers.

### 14.4 Late MSL Failures

Risk:

DXC and SPIRV-Cross may succeed, but Apple's Metal compiler may reject generated MSL.

Mitigation:

- Apple-host CI or local smoke tests must run `xcrun metal`.
- Do not declare a shader Apple-compatible until MSL compilation succeeds.
- Keep generated MSL in text form for inspection.

### 14.5 Toolchain Drift

Risk:

DXC, SPIRV-Cross, Xcode, and Metal Shader Converter versions can change behavior over time.

Mitigation:

- Pin tool versions when the shader pipeline becomes relied upon by runtime assets.
- Record tool versions in `.veshader.json`.
- Include generated output hashes in metadata.

## 15. Acceptance Criteria

The 2.2 milestone is complete when:

- `VEngineShaderTool` can compile one HLSL triangle shader to D3D11 DXBC, D3D12 DXIL, SPIR-V, MSL, and reflection JSON.
- Windows shader tests validate D3D11, D3D12, SPIR-V, MSL text generation, and metadata.
- Apple-host validation compiles generated MSL to `.metallib`.
- The D3D11, D3D12, and iOS Metal triangle demos can use artifacts generated from the same HLSL source.
- Reflection metadata drives at least one constant buffer binding and one texture/sampler binding.
- Runtime code loads precompiled artifacts and does not perform normal shader cross-compilation.

## 16. Reference Sources

- Microsoft DirectXShaderCompiler README: https://github.com/microsoft/DirectXShaderCompiler/blob/main/README.md
- Microsoft DirectXShaderCompiler HLSL to SPIR-V mapping: https://github.com/microsoft/DirectXShaderCompiler/blob/main/docs/SPIR-V.rst?plain=1
- Microsoft D3DCompile documentation: https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3dcompile
- KhronosGroup SPIRV-Cross README: https://github.com/KhronosGroup/SPIRV-Cross
- Apple Metal Shader Converter: https://developer.apple.com/metal/shader-converter/
- Apple Metal libraries: https://developer.apple.com/documentation/metal/metal-libraries
- Apple precompiled Metal shader libraries: https://developer.apple.com/documentation/metal/building-a-shader-library-by-precompiling-source-files
- Apple MTLLibrary: https://developer.apple.com/documentation/metal/mtllibrary
