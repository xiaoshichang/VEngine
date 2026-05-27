param(
    [string]$Tag = "v1.92.8"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceDir = Join-Path $Root "Source"

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

if (-not (Test-Path $SourceDir))
{
    New-Item -ItemType Directory -Force $Root | Out-Null
    Invoke-NativeCommand git @("clone", "--depth", "1", "--branch", $Tag, "https://github.com/ocornut/imgui.git", $SourceDir)
}
elseif (Test-Path (Join-Path $SourceDir ".git"))
{
    Invoke-NativeCommand git @("-C", $SourceDir, "fetch", "--depth", "1", "origin", "refs/tags/$Tag`:refs/tags/$Tag")
    Invoke-NativeCommand git @("-C", $SourceDir, "checkout", "--force", $Tag)
}
else
{
    throw "Dear ImGui Source exists but is not a git checkout: $SourceDir"
}

$RequiredFiles = @(
    "imgui.cpp",
    "imgui_demo.cpp",
    "imgui_draw.cpp",
    "imgui_tables.cpp",
    "imgui_widgets.cpp",
    "imgui.h",
    "backends\imgui_impl_win32.cpp",
    "backends\imgui_impl_win32.h"
)

foreach ($RequiredFile in $RequiredFiles)
{
    $RequiredPath = Join-Path $SourceDir $RequiredFile

    if (-not (Test-Path $RequiredPath))
    {
        throw "Dear ImGui checkout is missing required file: $RequiredPath"
    }
}

Write-Host "Dear ImGui source ready: $SourceDir"
