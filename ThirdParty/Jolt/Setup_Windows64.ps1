param(
    [string]$Tag = "v5.5.0",
    [string]$Configuration = "Debug",
    [switch]$BuildTestsAndDemos,
    [switch]$IncludeViewer,
    [string[]]$Targets = @()
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

Require-Command git
Require-Command cmake

if (-not (Test-Path $SourceDir))
{
    New-Item -ItemType Directory -Force $Root | Out-Null
    Invoke-NativeCommand git @("clone", "--depth", "1", "--branch", $Tag, "https://github.com/jrouwe/JoltPhysics.git", $SourceDir)
}
elseif (Test-Path (Join-Path $SourceDir ".git"))
{
    Invoke-NativeCommand git @("-C", $SourceDir, "fetch", "--depth", "1", "origin", "refs/tags/$Tag`:refs/tags/$Tag")
    Invoke-NativeCommand git @("-C", $SourceDir, "checkout", "--force", $Tag)
}
else
{
    throw "Jolt Physics Source exists but is not a git checkout: $SourceDir"
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
