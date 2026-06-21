param(
    [string]$SdkVersion = "10.0.26100.0",
    [string]$Sha256 = "005EFF830845789C7EFB2831A0B41950EE6954E9BCD93BAF50DE67AD537728B2"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$ToolDir = Join-Path $Root "Tools\x64"
$FxcExe = Join-Path $ToolDir "fxc.exe"

function Test-ExpectedHash
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (-not (Test-Path $Path))
    {
        return $false
    }

    $ActualHash = (Get-FileHash -Algorithm SHA256 $Path).Hash.ToUpperInvariant()
    return $ActualHash -eq $Sha256.ToUpperInvariant()
}

if (Test-ExpectedHash $FxcExe)
{
    Write-Host "FXC ready: $FxcExe"
    return
}

$SdkRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits\10\bin\$SdkVersion\x64"
$SdkFxcExe = Join-Path $SdkRoot "fxc.exe"

if (-not (Test-Path $SdkFxcExe))
{
    throw "fxc.exe was not found in the installed Windows SDK path: $SdkFxcExe"
}

if (-not (Test-ExpectedHash $SdkFxcExe))
{
    $ActualHash = (Get-FileHash -Algorithm SHA256 $SdkFxcExe).Hash.ToUpperInvariant()
    throw "Installed fxc.exe hash mismatch. Expected $Sha256, got $ActualHash."
}

New-Item -ItemType Directory -Force $ToolDir | Out-Null
Copy-Item -LiteralPath $SdkFxcExe -Destination $FxcExe -Force

if (-not (Test-ExpectedHash $FxcExe))
{
    throw "Copied fxc.exe hash mismatch after writing: $FxcExe"
}

Write-Host "FXC ready: $FxcExe"
