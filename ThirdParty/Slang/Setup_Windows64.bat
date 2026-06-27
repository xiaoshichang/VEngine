@echo off
setlocal EnableExtensions

set "ROOT=%CD%\ThirdParty\Slang\"
set "SLANG_EXE=%ROOT%windows64\bin\slangc.exe"
set "SLANG_DLL=%ROOT%windows64\bin\slang.dll"

if not exist "%SLANG_EXE%" (
    echo slangc.exe was not found: %SLANG_EXE%
    exit /b 1
)

if not exist "%SLANG_DLL%" (
    echo slang.dll was not found: %SLANG_DLL%
    exit /b 1
)

echo Slang ready: %SLANG_EXE%
exit /b 0
