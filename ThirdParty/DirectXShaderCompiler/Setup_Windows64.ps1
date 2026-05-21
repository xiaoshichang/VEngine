param(
    [string]$Version = "1.9.2602.17",
    [string]$Sha256 = "95703CA504F1C42B8FC3F0D1C4A7FED56BAE16299CF253D432BC90C24A86AE9A"
)

$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$PackageRoot = Join-Path $Root "Build\Windows64\$Version"
$PackageFile = Join-Path $PackageRoot "Microsoft.Direct3D.DXC.$Version.nupkg"
$ExtractDir = Join-Path $PackageRoot "Extract"
$LegacyPackageDir = Join-Path $PackageRoot "Package"
$ToolDir = Join-Path $PackageRoot "Tools\x64"
$DxcExe = Join-Path $ToolDir "dxc.exe"
$PackageUrl = "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.DXC/$Version"

New-Item -ItemType Directory -Force $PackageRoot | Out-Null

if (Test-Path $PackageFile)
{
    $ActualHash = (Get-FileHash -Algorithm SHA256 $PackageFile).Hash.ToUpperInvariant()

    if ($ActualHash -ne $Sha256.ToUpperInvariant())
    {
        Remove-Item -LiteralPath $PackageFile -Force
    }
}

if (-not (Test-Path $PackageFile))
{
    Write-Host "Downloading Microsoft.Direct3D.DXC $Version"
    Invoke-WebRequest -Uri $PackageUrl -OutFile $PackageFile
}

$ActualHash = (Get-FileHash -Algorithm SHA256 $PackageFile).Hash.ToUpperInvariant()

if ($ActualHash -ne $Sha256.ToUpperInvariant())
{
    throw "Microsoft.Direct3D.DXC package hash mismatch. Expected $Sha256, got $ActualHash."
}

if (-not (Test-Path $DxcExe))
{
    if (Test-Path $ExtractDir)
    {
        Remove-Item -LiteralPath $ExtractDir -Recurse -Force
    }

    if (Test-Path $LegacyPackageDir)
    {
        Remove-Item -LiteralPath $LegacyPackageDir -Recurse -Force
    }

    if (Test-Path $ToolDir)
    {
        Remove-Item -LiteralPath $ToolDir -Recurse -Force
    }

    $ArchiveFile = Join-Path $PackageRoot "Microsoft.Direct3D.DXC.$Version.zip"

    if (Test-Path $ArchiveFile)
    {
        Remove-Item -LiteralPath $ArchiveFile -Force
    }

    New-Item -ItemType Directory -Force $ExtractDir | Out-Null

    try
    {
        Copy-Item -LiteralPath $PackageFile -Destination $ArchiveFile -Force
        Expand-Archive -LiteralPath $ArchiveFile -DestinationPath $ExtractDir -Force

        $ExtractedToolDir = Join-Path $ExtractDir "build\native\bin\x64"

        if (-not (Test-Path (Join-Path $ExtractedToolDir "dxc.exe")))
        {
            throw "dxc.exe was not found inside the Microsoft.Direct3D.DXC package."
        }

        New-Item -ItemType Directory -Force $ToolDir | Out-Null
        Copy-Item -Path (Join-Path $ExtractedToolDir "*") -Destination $ToolDir -Recurse -Force
    }
    finally
    {
        if (Test-Path $ArchiveFile)
        {
            Remove-Item -LiteralPath $ArchiveFile -Force
        }

        if (Test-Path $ExtractDir)
        {
            Remove-Item -LiteralPath $ExtractDir -Recurse -Force
        }
    }
}
if (Test-Path $LegacyPackageDir)
{
    Remove-Item -LiteralPath $LegacyPackageDir -Recurse -Force
}

if (Test-Path $ExtractDir)
{
    Remove-Item -LiteralPath $ExtractDir -Recurse -Force
}

if (-not (Test-Path $DxcExe))
{
    throw "dxc.exe was not found after extracting Microsoft.Direct3D.DXC."
}

Write-Host "DXC ready: $DxcExe"
