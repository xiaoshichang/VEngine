@echo off
setlocal EnableExtensions

pushd "%~dp0"

python -m pip install wget
python main.py 1.85.0 Windows64
set "VE_EXIT_CODE=%ERRORLEVEL%"

popd
exit /b %VE_EXIT_CODE%
