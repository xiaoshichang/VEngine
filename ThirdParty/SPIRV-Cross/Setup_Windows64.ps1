param(
    [string]$Tag = "vulkan-sdk-1.4.309.0",
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceDir = Join-Path $Root "Source"
$BuildDir = Join-Path $Root "Build\Windows64\$Tag"

function Invoke-NativeCommand
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$FilePath,

        [string[]]$CommandArguments = @()
    )

    & $FilePath @CommandArguments

    if ($LASTEXITCODE -ne 0)
    {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

function Require-Command
{
    param([string]$Name)

    $Command = Get-Command $Name -ErrorAction SilentlyContinue

    if (-not $Command)
    {
        throw "$Name was not found in PATH."
    }
}

Require-Command git
Require-Command cmake

if (-not (Test-Path $SourceDir))
{
    New-Item -ItemType Directory -Force $Root | Out-Null
    Invoke-NativeCommand git @("clone", "--depth", "1", "--branch", $Tag, "https://github.com/KhronosGroup/SPIRV-Cross.git", $SourceDir)
}
elseif (Test-Path (Join-Path $SourceDir ".git"))
{
    Invoke-NativeCommand git @("-C", $SourceDir, "fetch", "--depth", "1", "origin", "refs/tags/$Tag`:refs/tags/$Tag")
    Invoke-NativeCommand git @("-C", $SourceDir, "checkout", "--force", $Tag)
}
else
{
    throw "SPIRV-Cross Source exists but is not a git checkout: $SourceDir"
}

if ($SkipBuild)
{
    Write-Host "SPIRV-Cross source ready: $SourceDir"
    exit 0
}

Invoke-NativeCommand cmake @(
    "-S", $SourceDir,
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-T", "v143",
    "-DSPIRV_CROSS_CLI=ON",
    "-DSPIRV_CROSS_ENABLE_TESTS=OFF",
    "-DSPIRV_CROSS_ENABLE_GLSL=ON",
    "-DSPIRV_CROSS_ENABLE_MSL=ON",
    "-DSPIRV_CROSS_ENABLE_REFLECT=ON",
    "-DSPIRV_CROSS_ENABLE_UTIL=ON",
    "-DSPIRV_CROSS_SHARED=OFF"
)

Invoke-NativeCommand cmake @("--build", $BuildDir, "--config", $Configuration, "--target", "spirv-cross")

$BuiltExe = Join-Path $BuildDir "$Configuration\spirv-cross.exe"

if (-not (Test-Path $BuiltExe))
{
    throw "SPIRV-Cross build completed, but spirv-cross.exe was not found: $BuiltExe"
}

Write-Host "SPIRV-Cross ready: $BuiltExe"
