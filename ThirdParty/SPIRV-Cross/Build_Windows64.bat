@echo off
setlocal EnableExtensions

call "%~dp0Setup_Windows64.bat" %*
exit /b %ERRORLEVEL%
