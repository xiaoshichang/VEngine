@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "TAG=v5.5.0"
set "ARCHIVE=JoltPhysics-5.5.0.zip"
set "CONFIGURATION=Debug"
set "BUILD_TESTS_AND_DEMOS=0"
set "INCLUDE_VIEWER=0"
set "TARGETS="

:ParseArguments
if "%~1"=="" goto ArgumentsParsed
if /I "%~1"=="-Tag" (
    if "%~2"=="" (
        echo Missing value for -Tag.
        exit /b 1
    )
    set "TAG=%~2"
    shift
    shift
    goto ParseArguments
)
if /I "%~1"=="-Archive" (
    if "%~2"=="" (
        echo Missing value for -Archive.
        exit /b 1
    )
    set "ARCHIVE=%~2"
    shift
    shift
    goto ParseArguments
)
if /I "%~1"=="-Configuration" (
    if "%~2"=="" (
        echo Missing value for -Configuration.
        exit /b 1
    )
    set "CONFIGURATION=%~2"
    shift
    shift
    goto ParseArguments
)
if /I "%~1"=="-BuildTestsAndDemos" (
    set "BUILD_TESTS_AND_DEMOS=1"
    shift
    goto ParseArguments
)
if /I "%~1"=="-IncludeViewer" (
    set "INCLUDE_VIEWER=1"
    shift
    goto ParseArguments
)
if /I "%~1"=="-Targets" (
    if "%~2"=="" (
        echo Missing value for -Targets.
        exit /b 1
    )
    if defined TARGETS (
        set "TARGETS=%TARGETS%,%~2"
    ) else (
        set "TARGETS=%~2"
    )
    shift
    shift
    goto ParseArguments
)

echo Unknown argument: %~1
exit /b 1

:ArgumentsParsed
where cmake >nul 2>nul || (
    echo cmake was not found in PATH.
    exit /b 1
)

set "ROOT=%CD%\ThirdParty\Jolt\"
set "SOURCE_DIR=%ROOT%Source"
set "BUILD_DIR=%ROOT%Build\Windows64\%TAG%"
set "VE_JOLT_ROOT=%ROOT%"
set "VE_JOLT_ARCHIVE=%ARCHIVE%"

call :TestJoltSource "%SOURCE_DIR%"
if "%JOLT_SOURCE_READY%"=="0" (
    if exist "%SOURCE_DIR%" (
        echo Jolt Physics source is incomplete, recreating from archive: %SOURCE_DIR%
        rd /s /q "%SOURCE_DIR%"
    )
    call :ExpandJoltArchive
    if errorlevel 1 exit /b 1
)

call :TestJoltSource "%SOURCE_DIR%"
if "%JOLT_SOURCE_READY%"=="0" (
    echo Jolt Physics Source exists but is not a valid source tree: %SOURCE_DIR%
    exit /b 1
)

if not "%BUILD_TESTS_AND_DEMOS%"=="1" (
    echo Jolt Physics source ready: %SOURCE_DIR%
    exit /b 0
)

if defined TARGETS (
    if /I "%TARGETS%"=="All" (
        set "BUILD_TARGETS=UnitTests HelloWorld PerformanceTest Samples JoltViewer"
    ) else (
        set "BUILD_TARGETS=%TARGETS:,= %"
    )
) else (
    set "BUILD_TARGETS=UnitTests HelloWorld PerformanceTest Samples"
    if "%INCLUDE_VIEWER%"=="1" set "BUILD_TARGETS=!BUILD_TARGETS! JoltViewer"
)

set "BUILD_UNIT_TESTS=OFF"
set "BUILD_HELLO_WORLD=OFF"
set "BUILD_PERFORMANCE_TEST=OFF"
set "BUILD_SAMPLES=OFF"
set "BUILD_VIEWER=OFF"

for %%T in (%BUILD_TARGETS%) do (
    if /I "%%T"=="UnitTests" set "BUILD_UNIT_TESTS=ON"
    if /I "%%T"=="HelloWorld" set "BUILD_HELLO_WORLD=ON"
    if /I "%%T"=="PerformanceTest" set "BUILD_PERFORMANCE_TEST=ON"
    if /I "%%T"=="Samples" set "BUILD_SAMPLES=ON"
    if /I "%%T"=="JoltViewer" set "BUILD_VIEWER=ON"
)

cmake -S "%SOURCE_DIR%\Build" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -T v143 ^
    -DBUILD_SHARED_LIBS=OFF ^
    -DENABLE_INSTALL=OFF ^
    -DUSE_STATIC_MSVC_RUNTIME_LIBRARY=OFF ^
    -DTARGET_UNIT_TESTS=%BUILD_UNIT_TESTS% ^
    -DTARGET_HELLO_WORLD=%BUILD_HELLO_WORLD% ^
    -DTARGET_PERFORMANCE_TEST=%BUILD_PERFORMANCE_TEST% ^
    -DTARGET_SAMPLES=%BUILD_SAMPLES% ^
    -DTARGET_VIEWER=%BUILD_VIEWER%
if errorlevel 1 exit /b 1

for %%T in (%BUILD_TARGETS%) do (
    cmake --build "%BUILD_DIR%" --config "%CONFIGURATION%" --target %%T
    if errorlevel 1 exit /b 1
)

echo Jolt Physics targets built: %BUILD_TARGETS%
echo Jolt Physics build directory: %BUILD_DIR%
exit /b 0

:TestJoltSource
set "JOLT_SOURCE_READY=1"
if not exist "%~1\Build\CMakeLists.txt" set "JOLT_SOURCE_READY=0"
if not exist "%~1\Jolt\Jolt.h" set "JOLT_SOURCE_READY=0"
if not exist "%~1\Jolt\Core\Core.h" set "JOLT_SOURCE_READY=0"
exit /b 0

:ExpandJoltArchive
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$ErrorActionPreference = 'Stop';" ^
    "$root = $env:VE_JOLT_ROOT;" ^
    "$archivePath = Join-Path $root $env:VE_JOLT_ARCHIVE;" ^
    "$sourceDir = Join-Path $root 'Source';" ^
    "$tempDir = Join-Path $root 'Source.extracting';" ^
    "if (-not (Test-Path -LiteralPath $archivePath)) { throw ('Jolt Physics archive was not found: ' + $archivePath) }" ^
    "if (Test-Path -LiteralPath $tempDir) { Remove-Item -LiteralPath $tempDir -Recurse -Force }" ^
    "New-Item -ItemType Directory -Force $tempDir | Out-Null;" ^
    "try { Expand-Archive -LiteralPath $archivePath -DestinationPath $tempDir -Force; $roots = @(Get-ChildItem -LiteralPath $tempDir -Directory); if ($roots.Count -ne 1) { throw ('Expected archive to contain one root directory, found ' + $roots.Count + '.') }; $requiredFiles = @('Build\CMakeLists.txt', 'Jolt\Jolt.h', 'Jolt\Core\Core.h'); foreach ($requiredFile in $requiredFiles) { if (-not (Test-Path -LiteralPath (Join-Path $roots[0].FullName $requiredFile))) { throw 'Archive does not contain a valid Jolt Physics source tree.' } }; Move-Item -LiteralPath $roots[0].FullName -Destination $sourceDir } finally { if (Test-Path -LiteralPath $tempDir) { Remove-Item -LiteralPath $tempDir -Recurse -Force } }"
exit /b %ERRORLEVEL%
