param(
    [string]$Tag = "v5.5.0",
    [string]$Archive = "JoltPhysics-5.5.0.zip",
    [string]$Configuration = "Debug",
    [switch]$BuildTestsAndDemos,
    [switch]$IncludeViewer,
    [string[]]$Targets = @()
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$SourceDir = Join-Path $Root "Source"
$BuildDir = Join-Path $Root "Build\Windows64\$Tag"
$ArchivePath = Join-Path $Root $Archive

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

function Test-JoltSource
{
    param([string]$Path)

    $RequiredFiles = @(
        "Build\CMakeLists.txt",
        "Jolt\Jolt.h",
        "Jolt\Core\Core.h"
    )

    foreach ($RequiredFile in $RequiredFiles)
    {
        if (-not (Test-Path (Join-Path $Path $RequiredFile)))
        {
            return $false
        }
    }

    return $true
}

function Expand-JoltArchive
{
    if (-not (Test-Path $ArchivePath))
    {
        throw "Jolt Physics archive was not found: $ArchivePath"
    }

    $TempDir = Join-Path $Root "Source.extracting"

    if (Test-Path $TempDir)
    {
        Remove-Item -Recurse -Force $TempDir
    }

    New-Item -ItemType Directory -Force $TempDir | Out-Null

    try
    {
        Expand-Archive -LiteralPath $ArchivePath -DestinationPath $TempDir -Force

        $ExtractedRoots = @(Get-ChildItem -LiteralPath $TempDir -Directory)

        if ($ExtractedRoots.Count -ne 1)
        {
            throw "Expected archive to contain one root directory, found $($ExtractedRoots.Count)."
        }

        if (-not (Test-JoltSource $ExtractedRoots[0].FullName))
        {
            throw "Archive does not contain a valid Jolt Physics source tree."
        }

        Move-Item -LiteralPath $ExtractedRoots[0].FullName -Destination $SourceDir
    }
    finally
    {
        if (Test-Path $TempDir)
        {
            Remove-Item -Recurse -Force $TempDir
        }
    }
}

function Normalize-TargetList
{
    param([string[]]$RawTargets)

    $NormalizedTargets = New-Object System.Collections.Generic.List[string]

    foreach ($RawTarget in $RawTargets)
    {
        foreach ($Target in $RawTarget.Split(",", [System.StringSplitOptions]::RemoveEmptyEntries))
        {
            $TrimmedTarget = $Target.Trim()

            if ($TrimmedTarget.Length -gt 0)
            {
                $NormalizedTargets.Add($TrimmedTarget)
            }
        }
    }

    return $NormalizedTargets.ToArray()
}

Require-Command cmake

if (-not (Test-Path $SourceDir))
{
    New-Item -ItemType Directory -Force $Root | Out-Null
    Expand-JoltArchive
}
elseif (-not (Test-JoltSource $SourceDir))
{
    Write-Host "Jolt Physics source is incomplete, recreating from archive: $SourceDir"
    Remove-Item -Recurse -Force $SourceDir
    Expand-JoltArchive
}

if (-not (Test-JoltSource $SourceDir))
{
    throw "Jolt Physics Source exists but is not a valid source tree: $SourceDir"
}

if (-not $BuildTestsAndDemos)
{
    Write-Host "Jolt Physics source ready: $SourceDir"
    exit 0
}

$BuildTargets = Normalize-TargetList $Targets

if ($BuildTargets.Count -eq 0)
{
    $BuildTargets = @("UnitTests", "HelloWorld", "PerformanceTest", "Samples")

    if ($IncludeViewer)
    {
        $BuildTargets += "JoltViewer"
    }
}
elseif ($BuildTargets -contains "All")
{
    $BuildTargets = @("UnitTests", "HelloWorld", "PerformanceTest", "Samples", "JoltViewer")
}

$BuildUnitTests = $BuildTargets -contains "UnitTests"
$BuildHelloWorld = $BuildTargets -contains "HelloWorld"
$BuildPerformanceTest = $BuildTargets -contains "PerformanceTest"
$BuildSamples = $BuildTargets -contains "Samples"
$BuildViewer = $BuildTargets -contains "JoltViewer"

Invoke-NativeCommand cmake @(
    "-S", (Join-Path $SourceDir "Build"),
    "-B", $BuildDir,
    "-G", "Visual Studio 17 2022",
    "-A", "x64",
    "-T", "v143",
    "-DBUILD_SHARED_LIBS=OFF",
    "-DENABLE_INSTALL=OFF",
    "-DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF",
    "-DTARGET_UNIT_TESTS=$BuildUnitTests",
    "-DTARGET_HELLO_WORLD=$BuildHelloWorld",
    "-DTARGET_PERFORMANCE_TEST=$BuildPerformanceTest",
    "-DTARGET_SAMPLES=$BuildSamples",
    "-DTARGET_VIEWER=$BuildViewer"
)

foreach ($BuildTarget in $BuildTargets)
{
    Invoke-NativeCommand cmake @("--build", $BuildDir, "--config", $Configuration, "--target", $BuildTarget)
}

Write-Host "Jolt Physics targets built: $($BuildTargets -join ", ")"
Write-Host "Jolt Physics build directory: $BuildDir"
