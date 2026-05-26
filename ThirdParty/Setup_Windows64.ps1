param(
    [switch]$SkipBoost,
    [switch]$SkipImGui
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepositoryRoot = Split-Path -Parent $Root
$WithMsvc = Join-Path $RepositoryRoot "CMake\Scripts\WithMsvc.bat"

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

function Test-BoostReady
{
    $BoostLibDir = Join-Path $Root "Boost\Build\Windows64\lib"
    $RequiredFiles = @(
        (Join-Path $BoostLibDir "cmake\Boost-1.85.0\BoostConfig.cmake"),
        (Join-Path $BoostLibDir "libboost_json-vc143-mt-x64-1_85.lib"),
        (Join-Path $BoostLibDir "libboost_json-vc143-mt-gd-x64-1_85.lib"),
        (Join-Path $BoostLibDir "libboost_log-vc143-mt-x64-1_85.lib"),
        (Join-Path $BoostLibDir "libboost_log-vc143-mt-gd-x64-1_85.lib"),
        (Join-Path $BoostLibDir "libboost_log_setup-vc143-mt-x64-1_85.lib"),
        (Join-Path $BoostLibDir "libboost_log_setup-vc143-mt-gd-x64-1_85.lib"),
        (Join-Path $BoostLibDir "libboost_system-vc143-mt-x64-1_85.lib"),
        (Join-Path $BoostLibDir "libboost_system-vc143-mt-gd-x64-1_85.lib")
    )

    foreach ($RequiredFile in $RequiredFiles)
    {
        if (-not (Test-Path $RequiredFile))
        {
            return $false
        }
    }

    return $true
}

if (-not $SkipBoost)
{
    if (-not (Test-BoostReady))
    {
        Push-Location (Join-Path $Root "Boost")

        try
        {
            Invoke-NativeCommand python @("-m", "pip", "install", "wget")
            Invoke-NativeCommand $WithMsvc @("python", "main.py", "1.85.0", "Windows64")
        }
        finally
        {
            Pop-Location
        }
    }
    else
    {
        Write-Host "Boost already prepared: $(Join-Path $Root "Boost\Build\Windows64")"
    }
}

if (-not $SkipImGui)
{
    & (Join-Path $Root "ImGui\Setup_Windows64.ps1")
}

& (Join-Path $Root "DirectXShaderCompiler\Setup_Windows64.ps1")
& (Join-Path $Root "SPIRV-Cross\Setup_Windows64.ps1")

Write-Host "ThirdParty Windows64 setup complete."
