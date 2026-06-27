@echo off
setlocal EnableExtensions

if not "%~1"=="" (
    echo DotNet Windows64 setup does not accept command line arguments. The runtime version is pinned by the project script.
    exit /b 1
)

call "%~dp0Setup_Windows64.bat"
exit /b %ERRORLEVEL%
