@echo off
setlocal EnableExtensions

for %%I in ("%~f0") do set "SCRIPT_ROOT=%%~dpI"
for %%I in ("%SCRIPT_ROOT%..\..") do set "REPOSITORY_ROOT=%%~fI"
set "DOTNET_SETUP_SCRIPT=%REPOSITORY_ROOT%\ThirdParty\DotNet\Setup_Windows64.bat"
set "DOTNET_BUILD_SCRIPT=%REPOSITORY_ROOT%\ThirdParty\DotNet\Build_Windows64.bat"
set "ROOT_SETUP_SCRIPT=%REPOSITORY_ROOT%\ThirdParty\Setup_Windows64.bat"

if not exist "%DOTNET_SETUP_SCRIPT%" (
    echo DotNet setup script is missing.
    exit /b 1
)

if not exist "%DOTNET_BUILD_SCRIPT%" (
    echo DotNet build wrapper is missing.
    exit /b 1
)

findstr /C:"VE_DOTNET_RUNTIME_VERSION=10.0.9" "%DOTNET_SETUP_SCRIPT%" >nul || (
    echo DotNet runtime version must be pinned to 10.0.9 in the setup script.
    exit /b 1
)

findstr /C:"VE_DOTNET_RUNTIME_RID=win-x64" "%DOTNET_SETUP_SCRIPT%" >nul || (
    echo DotNet runtime RID must be pinned to win-x64 in the setup script.
    exit /b 1
)

findstr /C:"VE_DOTNET_RUNTIME_FILE_NAME=dotnet-runtime-win-x64.zip" "%DOTNET_SETUP_SCRIPT%" >nul || (
    echo DotNet runtime archive name must be pinned in the setup script.
    exit /b 1
)

findstr /C:"DotNet\Setup_Windows64.bat" "%ROOT_SETUP_SCRIPT%" >nul || (
    echo Root ThirdParty setup must call the DotNet setup script.
    exit /b 1
)

echo DotNet third-party setup checks passed.
exit /b 0
