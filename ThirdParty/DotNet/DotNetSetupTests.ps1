param(
    [string]$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
)

$ErrorActionPreference = "Stop"

function Assert-True
{
    param(
        [Parameter(Mandatory = $true)]
        [bool]$Condition,

        [Parameter(Mandatory = $true)]
        [string]$Message
    )

    if (-not $Condition)
    {
        throw $Message
    }
}

$DotNetSetupScript = Join-Path $RepositoryRoot "ThirdParty\DotNet\main.py"
$DotNetBuildScript = Join-Path $RepositoryRoot "ThirdParty\DotNet\Build_Windows64.bat"
$RootSetupScript = Join-Path $RepositoryRoot "ThirdParty\Build_Windows64.bat"

Assert-True (Test-Path $DotNetSetupScript) "DotNet setup script is missing."
Assert-True (Test-Path $DotNetBuildScript) "DotNet build wrapper is missing."

$Command = Get-Command $DotNetSetupScript
$ParameterNames = @($Command.Parameters.Keys)

Assert-True (-not ($ParameterNames -contains "Version")) "DotNet setup script must not expose a Version parameter."
Assert-True (-not ($ParameterNames -contains "ChannelVersion")) "DotNet setup script must not expose a channel override parameter."

$DotNetSetupText = Get-Content -LiteralPath $DotNetSetupScript -Raw
$RootSetupText = Get-Content -LiteralPath $RootSetupScript -Raw

Assert-True ($DotNetSetupText.Contains('$DotNetRuntimeVersion = "10.0.9"')) "DotNet runtime version must be pinned to 10.0.9 in the setup script."
Assert-True ($DotNetSetupText.Contains('$DotNetRuntimeRid = "win-x64"')) "DotNet runtime RID must be pinned to win-x64 in the setup script."
Assert-True ($DotNetSetupText.Contains('$DotNetRuntimeFileName = "dotnet-runtime-win-x64.zip"')) "DotNet runtime archive name must be pinned in the setup script."
Assert-True ($RootSetupText.Contains('==> boost')) "Root ThirdParty build must invoke the Boost setup step."

Write-Host "DotNet third-party setup checks passed."
