$ErrorActionPreference = "Stop"

$DotNetRuntimeVersion = "10.0.9"
$DotNetRuntimeRid = "win-x64"
$DotNetRuntimeFileName = "dotnet-runtime-win-x64.zip"
$ReleaseMetadataUrl = "https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json"

$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
$DownloadRoot = Join-Path $Root "Downloads"
$ExtractRoot = Join-Path $Root "Extract"
$RuntimeRoot = Join-Path $Root "$DotNetRuntimeRid\$DotNetRuntimeVersion"
$ArchiveFile = Join-Path $DownloadRoot "dotnet-runtime-$DotNetRuntimeVersion-$DotNetRuntimeRid.zip"
$ExtractDir = Join-Path $ExtractRoot "$DotNetRuntimeVersion-$DotNetRuntimeRid"
$DotNetExe = Join-Path $RuntimeRoot "dotnet.exe"
$HostRoot = Join-Path $RuntimeRoot "host\fxr"
$SharedRoot = Join-Path $RuntimeRoot "shared\Microsoft.NETCore.App\$DotNetRuntimeVersion"
$CoreClrDll = Join-Path $SharedRoot "coreclr.dll"

function Get-NormalizedFullPath
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    return [System.IO.Path]::GetFullPath($Path)
}

function Assert-PathUnderRoot
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$AllowedRoot
    )

    $FullPath = Get-NormalizedFullPath $Path
    $FullRoot = Get-NormalizedFullPath $AllowedRoot

    if (-not $FullRoot.EndsWith([System.IO.Path]::DirectorySeparatorChar))
    {
        $FullRoot = "$FullRoot$([System.IO.Path]::DirectorySeparatorChar)"
    }

    if (-not $FullPath.StartsWith($FullRoot, [System.StringComparison]::OrdinalIgnoreCase))
    {
        throw "Refusing to operate outside DotNet third-party root. Path: $FullPath Root: $FullRoot"
    }
}

function Remove-DirectoryUnderRoot
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    if (Test-Path $Path)
    {
        Assert-PathUnderRoot -Path $Path -AllowedRoot $Root
        Remove-Item -LiteralPath $Path -Recurse -Force
    }
}

function Get-DotNetRuntimeFile
{
    Write-Host "Reading .NET $DotNetRuntimeVersion release metadata."
    $Metadata = Invoke-RestMethod -Uri $ReleaseMetadataUrl
    $Release = $Metadata.releases | Where-Object { $_.'release-version' -eq $DotNetRuntimeVersion } | Select-Object -First 1

    if ($null -eq $Release)
    {
        throw ".NET release $DotNetRuntimeVersion was not found in release metadata: $ReleaseMetadataUrl"
    }

    $RuntimeFile = $Release.runtime.files | Where-Object { $_.rid -eq $DotNetRuntimeRid -and $_.name -eq $DotNetRuntimeFileName } | Select-Object -First 1

    if ($null -eq $RuntimeFile)
    {
        throw ".NET runtime file $DotNetRuntimeFileName for $DotNetRuntimeRid was not found in release $DotNetRuntimeVersion."
    }

    return $RuntimeFile
}

function Test-ExpectedHash
{
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,

        [Parameter(Mandatory = $true)]
        [string]$ExpectedHash
    )

    if (-not (Test-Path $Path))
    {
        return $false
    }

    $ActualHash = (Get-FileHash -Algorithm SHA512 $Path).Hash.ToLowerInvariant()
    return $ActualHash -eq $ExpectedHash.ToLowerInvariant()
}

function Test-DotNetRuntimeReady
{
    if (-not (Test-Path $DotNetExe))
    {
        return $false
    }

    if (-not (Test-Path $CoreClrDll))
    {
        return $false
    }

    if (-not (Test-Path $HostRoot))
    {
        return $false
    }

    $HostFxrDll = Get-ChildItem -LiteralPath $HostRoot -Recurse -Filter "hostfxr.dll" -File -ErrorAction SilentlyContinue | Select-Object -First 1
    return $null -ne $HostFxrDll
}

if (Test-DotNetRuntimeReady)
{
    Write-Host ".NET runtime ready: $RuntimeRoot"
    return
}

$RuntimeFile = Get-DotNetRuntimeFile
$ExpectedHash = [string]$RuntimeFile.hash
$RuntimeUrl = [string]$RuntimeFile.url

if ([string]::IsNullOrWhiteSpace($ExpectedHash))
{
    throw ".NET runtime metadata is missing the SHA512 hash."
}

if ([string]::IsNullOrWhiteSpace($RuntimeUrl))
{
    throw ".NET runtime metadata is missing the download URL."
}

New-Item -ItemType Directory -Force $DownloadRoot | Out-Null

if ((Test-Path $ArchiveFile) -and -not (Test-ExpectedHash -Path $ArchiveFile -ExpectedHash $ExpectedHash))
{
    Remove-Item -LiteralPath $ArchiveFile -Force
}

if (-not (Test-Path $ArchiveFile))
{
    Write-Host "Downloading .NET runtime $DotNetRuntimeVersion for $DotNetRuntimeRid"
    Invoke-WebRequest -Uri $RuntimeUrl -OutFile $ArchiveFile
}

if (-not (Test-ExpectedHash -Path $ArchiveFile -ExpectedHash $ExpectedHash))
{
    $ActualHash = (Get-FileHash -Algorithm SHA512 $ArchiveFile).Hash.ToLowerInvariant()
    throw ".NET runtime package hash mismatch. Expected $ExpectedHash, got $ActualHash."
}

Remove-DirectoryUnderRoot $ExtractDir
Remove-DirectoryUnderRoot $RuntimeRoot

New-Item -ItemType Directory -Force $ExtractDir | Out-Null
New-Item -ItemType Directory -Force $RuntimeRoot | Out-Null

try
{
    Expand-Archive -LiteralPath $ArchiveFile -DestinationPath $ExtractDir -Force

    if (-not (Test-Path (Join-Path $ExtractDir "dotnet.exe")))
    {
        throw "dotnet.exe was not found inside the .NET runtime archive."
    }

    Copy-Item -Path (Join-Path $ExtractDir "*") -Destination $RuntimeRoot -Recurse -Force
}
finally
{
    Remove-DirectoryUnderRoot $ExtractDir
}

if (-not (Test-DotNetRuntimeReady))
{
    throw ".NET runtime was not ready after extraction: $RuntimeRoot"
}

Write-Host ".NET runtime ready: $RuntimeRoot"
