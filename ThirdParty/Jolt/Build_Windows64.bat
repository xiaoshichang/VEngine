@echo off
setlocal EnableExtensions

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Setup_Windows64.ps1" -BuildTestsAndDemos %*
exit /b %ERRORLEVEL%
