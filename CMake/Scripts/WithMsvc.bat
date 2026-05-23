@echo off
setlocal EnableExtensions

if "%~1"=="" (
    echo Usage: %~nx0 ^<command^> [args...]
    echo Example: %~nx0 cmake --preset windows-msvc-debug
    exit /b 1
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%VSWHERE%" (
    echo vswhere.exe was not found.
    echo Install Visual Studio 2022 or Build Tools 2022 with Desktop development with C++.
    exit /b 1
)

for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
    set "VSINSTALL=%%I"
)

if not defined VSINSTALL (
    echo Visual Studio 2022 C++ x64 tools were not found.
    echo Install Visual Studio 2022 Build Tools or Visual Studio 2022 with Desktop development with C++.
    echo Required component: MSVC v143 x64/x86 build tools.
    exit /b 1
)

set "VE_V143_TOOLSET=%VSINSTALL%\MSBuild\Microsoft\VC\v170\Platforms\x64\PlatformToolsets\v143"

if not exist "%VE_V143_TOOLSET%" (
    echo MSVC v143 platform toolset was not found at "%VE_V143_TOOLSET%".
    echo Install the MSVC v143 x64/x86 build tools in the Visual Studio 2022 or Build Tools 2022 installation.
    exit /b 1
)

set "VSDEVCMD=%VSINSTALL%\Common7\Tools\VsDevCmd.bat"

if not exist "%VSDEVCMD%" (
    echo VsDevCmd.bat was not found at "%VSDEVCMD%".
    exit /b 1
)

call "%VSDEVCMD%" -arch=x64 -host_arch=x64

if errorlevel 1 (
    echo Failed to initialize the Visual Studio developer environment.
    exit /b 1
)

where cl.exe >nul 2>nul

if errorlevel 1 (
    echo cl.exe was not found after initializing the Visual Studio developer environment.
    echo Install MSVC v143 x64/x86 build tools through the Visual Studio Installer.
    exit /b 1
)

if not defined WindowsSdkDir (
    echo WindowsSdkDir is not defined after initializing the Visual Studio developer environment.
    echo Install a Windows 10 SDK or Windows 11 SDK through the Visual Studio Installer.
    exit /b 1
)

if not defined WindowsSDKVersion (
    echo WindowsSDKVersion is not defined after initializing the Visual Studio developer environment.
    echo Install a Windows 10 SDK or Windows 11 SDK through the Visual Studio Installer.
    exit /b 1
)

set "VE_KERNEL32_LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64\kernel32.lib"

if not exist "%VE_KERNEL32_LIB%" (
    echo kernel32.lib was not found at "%VE_KERNEL32_LIB%".
    echo Install a Windows 10 SDK or Windows 11 SDK through the Visual Studio Installer.
    exit /b 1
)

%*
set "VE_COMMAND_EXIT_CODE=%ERRORLEVEL%"
exit /b %VE_COMMAND_EXIT_CODE%
