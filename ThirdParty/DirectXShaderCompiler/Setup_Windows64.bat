@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "VERSION=1.9.2602.17"
set "SHA256=95703CA504F1C42B8FC3F0D1C4A7FED56BAE16299CF253D432BC90C24A86AE9A"

:ParseArguments
if "%~1"=="" goto ArgumentsParsed
if /I "%~1"=="-Version" (
    if "%~2"=="" (
        echo Missing value for -Version.
        exit /b 1
    )
    set "VERSION=%~2"
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
set "ROOT=%CD%\ThirdParty\DirectXShaderCompiler\"
set "PACKAGE_ROOT=%ROOT%Build\Windows64\%VERSION%"
set "PACKAGE_FILE=%PACKAGE_ROOT%\Microsoft.Direct3D.DXC.%VERSION%.nupkg"
set "EXTRACT_DIR=%PACKAGE_ROOT%\Extract"
set "LEGACY_PACKAGE_DIR=%PACKAGE_ROOT%\Package"
set "TOOL_DIR=%PACKAGE_ROOT%\Tools\x64"
set "DXC_EXE=%TOOL_DIR%\dxc.exe"
set "PACKAGE_URL=https://www.nuget.org/api/v2/package/Microsoft.Direct3D.DXC/%VERSION%"

if not exist "%PACKAGE_ROOT%" mkdir "%PACKAGE_ROOT%" || exit /b 1

if exist "%PACKAGE_FILE%" (
    call :GetSha256 "%PACKAGE_FILE%"
    if /I not "!ACTUAL_HASH!"=="%SHA256%" del /f /q "%PACKAGE_FILE%"
)

if not exist "%PACKAGE_FILE%" (
    echo Downloading Microsoft.Direct3D.DXC %VERSION%
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Invoke-WebRequest -Uri '%PACKAGE_URL%' -OutFile $env:PACKAGE_FILE"
    if errorlevel 1 exit /b 1
)

call :GetSha256 "%PACKAGE_FILE%"
if /I not "%ACTUAL_HASH%"=="%SHA256%" (
    echo Microsoft.Direct3D.DXC package hash mismatch. Expected %SHA256%, got %ACTUAL_HASH%.
    exit /b 1
)

if not exist "%DXC_EXE%" (
    if exist "%EXTRACT_DIR%" rd /s /q "%EXTRACT_DIR%"
    if exist "%LEGACY_PACKAGE_DIR%" rd /s /q "%LEGACY_PACKAGE_DIR%"
    if exist "%TOOL_DIR%" rd /s /q "%TOOL_DIR%"

    set "ARCHIVE_FILE=%PACKAGE_ROOT%\Microsoft.Direct3D.DXC.%VERSION%.zip"
    if exist "!ARCHIVE_FILE!" del /f /q "!ARCHIVE_FILE!"
    mkdir "%EXTRACT_DIR%" || exit /b 1

    copy /y "%PACKAGE_FILE%" "!ARCHIVE_FILE!" >nul || exit /b 1
    powershell -NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath $env:ARCHIVE_FILE -DestinationPath $env:EXTRACT_DIR -Force"
    if errorlevel 1 exit /b 1

    set "EXTRACTED_TOOL_DIR=%EXTRACT_DIR%\build\native\bin\x64"
    if not exist "!EXTRACTED_TOOL_DIR!\dxc.exe" (
        echo dxc.exe was not found inside the Microsoft.Direct3D.DXC package.
        exit /b 1
    )

    mkdir "%TOOL_DIR%" || exit /b 1
    xcopy "!EXTRACTED_TOOL_DIR!\*" "%TOOL_DIR%\" /E /I /Y >nul
    if errorlevel 1 exit /b 1

    if exist "!ARCHIVE_FILE!" del /f /q "!ARCHIVE_FILE!"
    if exist "%EXTRACT_DIR%" rd /s /q "%EXTRACT_DIR%"
)

if exist "%LEGACY_PACKAGE_DIR%" rd /s /q "%LEGACY_PACKAGE_DIR%"
if exist "%EXTRACT_DIR%" rd /s /q "%EXTRACT_DIR%"

if not exist "%DXC_EXE%" (
    echo dxc.exe was not found after extracting Microsoft.Direct3D.DXC.
    exit /b 1
)

echo DXC ready: %DXC_EXE%
exit /b 0

:GetSha256
set "ACTUAL_HASH="
for /f "usebackq delims=" %%H in (`powershell -NoProfile -ExecutionPolicy Bypass -Command "(Get-FileHash -Algorithm SHA256 -LiteralPath '%~1').Hash.ToUpperInvariant()"`) do set "ACTUAL_HASH=%%H"
exit /b 0
