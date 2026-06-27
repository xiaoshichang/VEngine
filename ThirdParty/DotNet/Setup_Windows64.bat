@echo off
setlocal EnableExtensions

if not "%~1"=="" (
    echo DotNet Windows64 setup does not accept command line arguments. The runtime version is pinned by the project script.
    exit /b 1
)

set "VE_DOTNET_SETUP_ROOT=%CD%\ThirdParty\DotNet\"
set "VE_DOTNET_RUNTIME_VERSION=10.0.9"
set "VE_DOTNET_RUNTIME_RID=win-x64"
set "VE_DOTNET_RUNTIME_FILE_NAME=dotnet-runtime-win-x64.zip"
set "VE_DOTNET_RELEASE_METADATA_URL=https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "$root = $env:VE_DOTNET_SETUP_ROOT;" ^
    "$version = $env:VE_DOTNET_RUNTIME_VERSION;" ^
    "$rid = $env:VE_DOTNET_RUNTIME_RID;" ^
    "$runtimeFileName = $env:VE_DOTNET_RUNTIME_FILE_NAME;" ^
    "$metadataUrl = $env:VE_DOTNET_RELEASE_METADATA_URL;" ^
    "$downloadRoot = Join-Path $root 'Downloads';" ^
    "$extractRoot = Join-Path $root 'Extract';" ^
    "$runtimeRoot = Join-Path $root (Join-Path $rid $version);" ^
    "$archiveFile = Join-Path $downloadRoot ('dotnet-runtime-' + $version + '-' + $rid + '.zip');" ^
    "$extractDir = Join-Path $extractRoot ($version + '-' + $rid);" ^
    "$dotnetExe = Join-Path $runtimeRoot 'dotnet.exe';" ^
    "$hostRoot = Join-Path $runtimeRoot 'host\fxr';" ^
    "$sharedRoot = Join-Path $runtimeRoot ('shared\Microsoft.NETCore.App\' + $version);" ^
    "$coreClrDll = Join-Path $sharedRoot 'coreclr.dll';" ^
    "$runtimeReady = (Test-Path -LiteralPath $dotnetExe) -and (Test-Path -LiteralPath $coreClrDll) -and (Test-Path -LiteralPath $hostRoot) -and ($null -ne (Get-ChildItem -LiteralPath $hostRoot -Recurse -Filter 'hostfxr.dll' -File -ErrorAction SilentlyContinue | Select-Object -First 1));" ^
    "if ($runtimeReady) { Write-Host ('.NET runtime ready: ' + $runtimeRoot); exit 0 }" ^
    "Write-Host ('Reading .NET ' + $version + ' release metadata.');" ^
    "$metadata = Invoke-RestMethod -Uri $metadataUrl;" ^
    "$release = $metadata.releases | Where-Object { $_.'release-version' -eq $version } | Select-Object -First 1;" ^
    "if ($null -eq $release) { throw ('.NET release ' + $version + ' was not found in release metadata: ' + $metadataUrl) }" ^
    "$runtimeFile = $release.runtime.files | Where-Object { $_.rid -eq $rid -and $_.name -eq $runtimeFileName } | Select-Object -First 1;" ^
    "if ($null -eq $runtimeFile) { throw ('.NET runtime file ' + $runtimeFileName + ' for ' + $rid + ' was not found in release ' + $version + '.') }" ^
    "$expectedHash = [string]$runtimeFile.hash;" ^
    "$runtimeUrl = [string]$runtimeFile.url;" ^
    "if ([string]::IsNullOrWhiteSpace($expectedHash)) { throw '.NET runtime metadata is missing the SHA512 hash.' }" ^
    "if ([string]::IsNullOrWhiteSpace($runtimeUrl)) { throw '.NET runtime metadata is missing the download URL.' }" ^
    "New-Item -ItemType Directory -Force $downloadRoot | Out-Null;" ^
    "if ((Test-Path -LiteralPath $archiveFile) -and ((Get-FileHash -Algorithm SHA512 -LiteralPath $archiveFile).Hash.ToLowerInvariant() -ne $expectedHash.ToLowerInvariant())) { Remove-Item -LiteralPath $archiveFile -Force }" ^
    "if (-not (Test-Path -LiteralPath $archiveFile)) { Write-Host ('Downloading .NET runtime ' + $version + ' for ' + $rid); Invoke-WebRequest -Uri $runtimeUrl -OutFile $archiveFile }" ^
    "$actualHash = (Get-FileHash -Algorithm SHA512 -LiteralPath $archiveFile).Hash.ToLowerInvariant();" ^
    "if ($actualHash -ne $expectedHash.ToLowerInvariant()) { throw ('.NET runtime package hash mismatch. Expected ' + $expectedHash + ', got ' + $actualHash + '.') }" ^
    "if (Test-Path -LiteralPath $extractDir) { Remove-Item -LiteralPath $extractDir -Recurse -Force }" ^
    "if (Test-Path -LiteralPath $runtimeRoot) { Remove-Item -LiteralPath $runtimeRoot -Recurse -Force }" ^
    "New-Item -ItemType Directory -Force $extractDir | Out-Null;" ^
    "New-Item -ItemType Directory -Force $runtimeRoot | Out-Null;" ^
    "try { Expand-Archive -LiteralPath $archiveFile -DestinationPath $extractDir -Force; if (-not (Test-Path -LiteralPath (Join-Path $extractDir 'dotnet.exe'))) { throw 'dotnet.exe was not found inside the .NET runtime archive.' }; Copy-Item -Path (Join-Path $extractDir '*') -Destination $runtimeRoot -Recurse -Force } finally { if (Test-Path -LiteralPath $extractDir) { Remove-Item -LiteralPath $extractDir -Recurse -Force } }" ^
    "$runtimeReady = (Test-Path -LiteralPath $dotnetExe) -and (Test-Path -LiteralPath $coreClrDll) -and (Test-Path -LiteralPath $hostRoot) -and ($null -ne (Get-ChildItem -LiteralPath $hostRoot -Recurse -Filter 'hostfxr.dll' -File -ErrorAction SilentlyContinue | Select-Object -First 1));" ^
    "if (-not $runtimeReady) { throw ('.NET runtime was not ready after extraction: ' + $runtimeRoot) }" ^
    "Write-Host ('.NET runtime ready: ' + $runtimeRoot)"

exit /b %ERRORLEVEL%
