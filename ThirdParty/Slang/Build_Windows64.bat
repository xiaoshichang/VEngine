@echo off
setlocal EnableExtensions

pushd "%~dp0"
python main.py %*
set "VE_EXIT_CODE=%ERRORLEVEL%"
popd
exit /b %VE_EXIT_CODE%
