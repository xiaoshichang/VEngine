@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "SDK_VERSION=10.0.26100.0"
set "SHA256=005EFF830845789C7EFB2831A0B41950EE6954E9BCD93BAF50DE67AD537728B2"

:ParseArguments
if "%~1"=="" goto ArgumentsParsed
if /I "%~1"=="-SdkVersion" (
    if "%~2"=="" (
        echo Missing value for -SdkVersion.
        exit /b 1
    )
    set "SDK_VERSION=%~2"
    shift
    shift
    goto ParseArguments
)
if /I "%~1"=="-Sha256" (
    if "%~2"=="" (
        echo Missing value for -Sha256.
        exit /b 1
    )
    set "SHA256=%~2"
    shift
    shift
    goto ParseArguments
)

echo Unknown argument: %~1
exit /b 1

:ArgumentsParsed
set "ROOT=%CD%\ThirdParty\WindowsSdkTools\"
set "TOOL_DIR=%ROOT%Tools\x64"
set "FXC_EXE=%TOOL_DIR%\fxc.exe"

call :TestExpectedHash "%FXC_EXE%"
if "%HASH_MATCH%"=="1" (
    echo FXC ready: %FXC_EXE%
    exit /b 0
)

set "SDK_ROOT=%ProgramFiles(x86)%\Windows Kits\10\bin\%SDK_VERSION%\x64"
set "SDK_FXC_EXE=%SDK_ROOT%\fxc.exe"

if not exist "%SDK_FXC_EXE%" (
    echo fxc.exe was not found in the installed Windows SDK path: %SDK_FXC_EXE%
    exit /b 1
)

call :TestExpectedHash "%SDK_FXC_EXE%"
if not "%HASH_MATCH%"=="1" (
    echo Installed fxc.exe hash mismatch. Expected %SHA256%, got !ACTUAL_HASH!.
    exit /b 1
)

if not exist "%TOOL_DIR%" mkdir "%TOOL_DIR%" || exit /b 1
copy /y "%SDK_FXC_EXE%" "%FXC_EXE%" >nul || exit /b 1

call :TestExpectedHash "%FXC_EXE%"
if not "%HASH_MATCH%"=="1" (
    echo Copied fxc.exe hash mismatch after writing: %FXC_EXE%
    exit /b 1
)

echo FXC ready: %FXC_EXE%
exit /b 0

:TestExpectedHash
set "HASH_MATCH=0"
set "ACTUAL_HASH="
if not exist "%~1" exit /b 0
for /f "usebackq delims=" %%H in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%~1').Hash.ToUpperInvariant()"`) do set "ACTUAL_HASH=%%H"
if /I "%ACTUAL_HASH%"=="%SHA256%" set "HASH_MATCH=1"
exit /b 0
