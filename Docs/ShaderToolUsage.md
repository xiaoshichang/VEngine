# VEngineShaderTool 使用教程

## 1. 用途

`VEngineShaderTool` 是 VEngine 当前阶段的离线 Shader 编译工具。它接收一份 HLSL 源文件，固定编译
`VSMain` 和 `PSMain` 两个入口，并输出 D3D11、D3D12、SPIR-V、Metal MSL 和反射数据。

当前版本的目标是验证 HLSL 到多后端 Shader 产物的基础流程，供 RHI demo、shader pipeline smoke test 和后续
资源系统接入使用。运行时 Player 不应依赖普通场景下的 shader 跨编译，运行时应加载这个工具提前生成的产物。

## 2. 当前能力

当前 `compile` 命令会为每个 shader 生成：

- D3D11 DXBC：通过 `fxc`，目标 profile 为 `vs_5_0` / `ps_5_0`。
- D3D12 DXIL：通过 `dxc`，目标 profile 为 `vs_6_0` / `ps_6_0`。
- SPIR-V：通过 `dxc -spirv`，目标环境为 `vulkan1.1`。
- Metal MSL 源码：通过 `spirv-cross --msl --msl-ios` 从 SPIR-V 生成。
- SPIRV-Cross 原始反射 JSON：每个 stage 一份。
- VEngine 归一化反射 JSON：每个 shader 一份 `.veshader.json`。

当前版本有一些刻意收窄的限制：

- 只支持 vertex shader 和 pixel shader。
- 入口函数固定为 `VSMain` 和 `PSMain`。
- 不支持 shader manifest。
- 不支持变体、宏定义参数、include search path 参数。
- 不支持 compute、geometry、tessellation、mesh、ray tracing shader。
- 当前工具链验证和 CMake 接入以 Windows64 为主。
- Metal 输出当前是 `.metal` MSL 源码，不是最终 `.metallib`。Apple 平台打包阶段后续再把 `.metal` 编译成 Metal library。

## 3. 准备工具链

在 Windows64 上先准备第三方工具和构建产物：

```bat
ThirdParty\Setup_Windows64.bat
```

这个脚本会准备：

- Boost：`ThirdParty\Boost\Build\Windows64`
- DXC：`ThirdParty\DirectXShaderCompiler\Build\Windows64\1.9.2602.17\Tools\x64\dxc.exe`
- SPIRV-Cross：`ThirdParty\SPIRV-Cross\Build\Windows64\vulkan-sdk-1.4.309.0\Release\spirv-cross.exe`

然后配置并构建工具：

```bat
cmd /c CMake\Scripts\WithMsvc.bat cmake --preset windows-msvc-release
cmd /c CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-release --target VEngineShaderTool
```

Release 版本工具路径：

```text
Build\windows-msvc-release\Release\VEngineShaderTool.exe
```

测试 preset 也会构建工具，Debug 版本工具路径：

```text
Build\windows-msvc-tests\Debug\VEngineShaderTool.exe
```

## 4. 命令格式

查看帮助：

```bat
Build\windows-msvc-release\Release\VEngineShaderTool.exe --help
```

编译命令：

```text
VEngineShaderTool compile
  --source <hlsl file>
  --output <output directory>
  --name <shader name>
  [--dxc <dxc executable>]
  [--fxc <fxc executable>]
  [--spirv-cross <spirv-cross executable>]
```

参数说明：

| 参数 | 必填 | 说明 |
| --- | --- | --- |
| `compile` | 是 | 执行 shader 编译流程。 |
| `--source` | 是 | 输入 HLSL 文件路径。 |
| `--output` | 是 | 输出目录。不存在时会自动创建。 |
| `--name` | 是 | Shader 名称，也会作为输出文件名前缀。 |
| `--dxc` | 否 | DXC 可执行文件路径。未指定时使用 PATH 中的 `dxc`。 |
| `--fxc` | 否 | FXC 可执行文件路径。未指定时使用 PATH 中的 `fxc`。 |
| `--spirv-cross` | 否 | SPIRV-Cross 可执行文件路径。未指定时使用 PATH 中的 `spirv-cross`。 |

推荐手动运行时显式传入 `--dxc` 和 `--spirv-cross`，并通过 `CMake\Scripts\WithMsvc.bat` 运行命令，让 Windows SDK
里的 `fxc` 自动出现在 PATH 中。

## 5. HLSL 输入约定

一个可被当前工具处理的 HLSL 文件需要满足这些约定：

- 顶点入口函数名为 `VSMain`。
- 像素入口函数名为 `PSMain`。
- 可绑定资源必须显式声明 `register`。
- 可绑定资源必须显式声明 `space`。
- 当前识别的可绑定资源包括：
  - `cbuffer`
  - `Texture*`，例如 `Texture2D`
  - `SamplerState`

示例：

```hlsl
cbuffer CameraConstants : register(b0, space0)
{
    row_major float4x4 viewProjection;
};

Texture2D BaseColorTexture : register(t0, space1);
SamplerState BaseColorSampler : register(s0, space1);
```

不要省略 `register` 或 `space`：

```hlsl
// 错误：缺少 register 和 space。
cbuffer CameraConstants
{
    row_major float4x4 viewProjection;
};
```

工具会在编译前检查这类声明。如果资源缺少显式绑定，会直接失败并输出类似错误：

```text
Bindable shader resource on line 8 must declare an explicit register.
```

## 6. 输出文件

假设执行：

```text
--name BasicTriangle
--output Build\Generated\ShaderExamples\BasicTriangle
```

工具会生成：

| 文件 | 说明 |
| --- | --- |
| `BasicTriangle.D3D11.hlsl` | 为 D3D11 生成的 HLSL。工具会移除 `register(..., spaceN)` 中的 `spaceN`，因为 FXC/D3D11 path 不使用 register space。 |
| `BasicTriangle.VS.dxbc` | Vertex shader 的 D3D11 DXBC。 |
| `BasicTriangle.PS.dxbc` | Pixel shader 的 D3D11 DXBC。 |
| `BasicTriangle.VS.dxil` | Vertex shader 的 D3D12 DXIL。 |
| `BasicTriangle.PS.dxil` | Pixel shader 的 D3D12 DXIL。 |
| `BasicTriangle.VS.spv` | Vertex shader 的 SPIR-V。 |
| `BasicTriangle.PS.spv` | Pixel shader 的 SPIR-V。 |
| `BasicTriangle.VS.metal` | Vertex shader 转换后的 Metal MSL 源码。 |
| `BasicTriangle.PS.metal` | Pixel shader 转换后的 Metal MSL 源码。 |
| `BasicTriangle.VS.reflect.json` | SPIRV-Cross 生成的 vertex stage 原始反射信息。 |
| `BasicTriangle.PS.reflect.json` | SPIRV-Cross 生成的 pixel stage 原始反射信息。 |
| `BasicTriangle.veshader.json` | VEngine 归一化反射信息。 |

`.veshader.json` 会记录 stage 产物路径和资源绑定信息，例如：

```json
{
  "schemaVersion": 1,
  "name": "BasicTriangle",
  "stages": [
    {
      "stage": "Vertex",
      "entry": "VSMain",
      "artifacts": {
        "d3d11": "Build/Generated/ShaderExamples/BasicTriangle/BasicTriangle.VS.dxbc",
        "d3d12": "Build/Generated/ShaderExamples/BasicTriangle/BasicTriangle.VS.dxil",
        "spirv": "Build/Generated/ShaderExamples/BasicTriangle/BasicTriangle.VS.spv",
        "metal": "Build/Generated/ShaderExamples/BasicTriangle/BasicTriangle.VS.metal"
      }
    }
  ],
  "resources": [
    {
      "name": "CameraConstants",
      "kind": "ConstantBuffer",
      "bindGroup": 0,
      "binding": 0,
      "hlslRegister": "b0",
      "hlslSpace": 0,
      "metalIndex": 0
    }
  ]
}
```

实际文件中会同时包含 Vertex 和 Pixel 两个 stage。

## 7. 示例一：编译仓库自带 BasicTriangle

仓库已有一个标准测试 shader：

```text
Tests\Shaders\HLSL\BasicTriangle.hlsl
```

从仓库根目录运行：

```bat
cmd /c CMake\Scripts\WithMsvc.bat ^
  Build\windows-msvc-release\Release\VEngineShaderTool.exe ^
  compile ^
  --source Tests\Shaders\HLSL\BasicTriangle.hlsl ^
  --output Build\Generated\ShaderExamples\BasicTriangle ^
  --name BasicTriangle ^
  --dxc ThirdParty\DirectXShaderCompiler\Build\Windows64\1.9.2602.17\Tools\x64\dxc.exe ^
  --fxc fxc ^
  --spirv-cross ThirdParty\SPIRV-Cross\Build\Windows64\vulkan-sdk-1.4.309.0\Release\spirv-cross.exe
```

成功时最后会输出：

```text
Shader flow complete: BasicTriangle
```

查看输出目录：

```bat
dir Build\Generated\ShaderExamples\BasicTriangle
```

可以重点检查：

```bat
type Build\Generated\ShaderExamples\BasicTriangle\BasicTriangle.veshader.json
type Build\Generated\ShaderExamples\BasicTriangle\BasicTriangle.PS.metal
```

## 8. 示例二：最小带常量缓冲的颜色 Shader

新建一份 HLSL，例如 `Build\Generated\ShaderExamples\SolidColor.hlsl`：

```hlsl
struct VertexInput
{
    float3 position : POSITION;
    float3 color : COLOR0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

cbuffer CameraConstants : register(b0, space0)
{
    row_major float4x4 viewProjection;
};

VertexOutput VSMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(float4(input.position, 1.0), viewProjection);
    output.color = input.color;
    return output;
}

float4 PSMain(VertexOutput input) : SV_Target
{
    return float4(input.color, 1.0);
}
```

编译：

```bat
cmd /c CMake\Scripts\WithMsvc.bat ^
  Build\windows-msvc-release\Release\VEngineShaderTool.exe ^
  compile ^
  --source Build\Generated\ShaderExamples\SolidColor.hlsl ^
  --output Build\Generated\ShaderExamples\SolidColor ^
  --name SolidColor ^
  --dxc ThirdParty\DirectXShaderCompiler\Build\Windows64\1.9.2602.17\Tools\x64\dxc.exe ^
  --fxc fxc ^
  --spirv-cross ThirdParty\SPIRV-Cross\Build\Windows64\vulkan-sdk-1.4.309.0\Release\spirv-cross.exe
```

输出文件会以 `SolidColor` 为前缀：

```text
SolidColor.VS.dxbc
SolidColor.PS.dxbc
SolidColor.VS.dxil
SolidColor.PS.dxil
SolidColor.VS.spv
SolidColor.PS.spv
SolidColor.VS.metal
SolidColor.PS.metal
SolidColor.VS.reflect.json
SolidColor.PS.reflect.json
SolidColor.veshader.json
```

## 9. 示例三：验证绑定错误

仓库中有一个故意缺少 `register` 的 shader：

```text
Tests\Shaders\HLSL\MissingRegister.hlsl
```

运行：

```bat
cmd /c CMake\Scripts\WithMsvc.bat ^
  Build\windows-msvc-release\Release\VEngineShaderTool.exe ^
  compile ^
  --source Tests\Shaders\HLSL\MissingRegister.hlsl ^
  --output Build\Generated\ShaderExamples\MissingRegister ^
  --name MissingRegister ^
  --dxc ThirdParty\DirectXShaderCompiler\Build\Windows64\1.9.2602.17\Tools\x64\dxc.exe ^
  --fxc fxc ^
  --spirv-cross ThirdParty\SPIRV-Cross\Build\Windows64\vulkan-sdk-1.4.309.0\Release\spirv-cross.exe
```

预期结果是失败。这个测试用于确认工具会拒绝没有显式绑定信息的资源声明。

## 10. 通过 CTest 运行现有 ShaderTool 测试

配置并构建测试 preset：

```bat
cmd /c CMake\Scripts\WithMsvc.bat cmake --preset windows-msvc-tests
cmd /c CMake\Scripts\WithMsvc.bat cmake --build --preset windows-msvc-tests
```

只运行 ShaderTool 相关测试：

```bat
cmd /c CMake\Scripts\WithMsvc.bat ctest --preset windows-msvc-tests -R VEngineShaderTool
```

当前测试包括：

- `VEngineShaderToolHelp`
- `VEngineShaderToolCompileBasicTriangle`
- `VEngineShaderToolValidateBasicTriangle`
- `VEngineShaderToolRejectsMissingRegister`

测试产物默认生成到：

```text
Build\windows-msvc-tests\Generated\ShaderTests\Debug
```

## 11. 常见问题

### 11.1 找不到 fxc

`fxc` 来自 Windows SDK。请通过 `CMake\Scripts\WithMsvc.bat` 运行命令，或显式传入 `--fxc` 的完整路径。

推荐：

```bat
cmd /c CMake\Scripts\WithMsvc.bat Build\windows-msvc-release\Release\VEngineShaderTool.exe --help
```

如果仍然找不到，需要通过 Visual Studio Installer 安装 Windows 10 SDK 或 Windows 11 SDK。

### 11.2 找不到 dxc

先运行：

```bat
ThirdParty\Setup_Windows64.bat
```

然后在编译命令中传入：

```text
--dxc ThirdParty\DirectXShaderCompiler\Build\Windows64\1.9.2602.17\Tools\x64\dxc.exe
```

### 11.3 找不到 spirv-cross

先运行：

```bat
ThirdParty\Setup_Windows64.bat
```

然后在编译命令中传入：

```text
--spirv-cross ThirdParty\SPIRV-Cross\Build\Windows64\vulkan-sdk-1.4.309.0\Release\spirv-cross.exe
```

### 11.4 提示资源必须声明 explicit register

所有 `cbuffer`、`Texture*`、`SamplerState` 都需要显式绑定：

```hlsl
cbuffer CameraConstants : register(b0, space0)
Texture2D BaseColorTexture : register(t0, space1);
SamplerState BaseColorSampler : register(s0, space1);
```

这条规则是为了让 D3D11、D3D12、Metal 和 VEngine 归一化反射能使用同一套绑定语义。

### 11.5 生成了 `.metal`，但没有 `.metallib`

这是当前预期行为。`VEngineShaderTool` 现在生成 MSL 源码，Apple 平台打包阶段后续会通过 Apple 的 `metal`
命令行工具把 `.metal` 编译成 `.metallib`。

## 12. 后续计划接口

后续可以在不破坏当前命令的前提下扩展：

- 增加 shader manifest。
- 增加 `--define` 和 `--include-dir`。
- 增加 shader variant 生成。
- 增加 Metal library 打包步骤。
- 增加 compute shader。
- 将 `.veshader.json` 与资源系统、材质系统、Pipeline State 创建流程正式打通。
