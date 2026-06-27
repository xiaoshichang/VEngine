@echo off
setlocal
call "%~dp0Setup_Windows64.bat" %*
exit /b %ERRORLEVEL%
