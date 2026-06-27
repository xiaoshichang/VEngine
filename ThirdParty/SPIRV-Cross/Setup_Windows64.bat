@echo off
setlocal EnableExtensions

set "TAG=vulkan-sdk-1.4.309.0"
set "CONFIGURATION=Release"
set "SKIP_BUILD=0"

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
if /I "%~1"=="-SkipBuild" (
    set "SKIP_BUILD=1"
    shift
    goto ParseArguments
)

echo Unknown argument: %~1
exit /b 1

:ArgumentsParsed
where git >nul 2>nul || (
    echo git was not found in PATH.
    exit /b 1
)
where cmake >nul 2>nul || (
    echo cmake was not found in PATH.
    exit /b 1
)

set "ROOT=%CD%\ThirdParty\SPIRV-Cross\"
set "SOURCE_DIR=%ROOT%Source"
set "BUILD_DIR=%ROOT%Build\Windows64\%TAG%"

if not exist "%SOURCE_DIR%" (
    if not exist "%ROOT%" mkdir "%ROOT%" || exit /b 1
    git clone --depth 1 --branch "%TAG%" https://github.com/KhronosGroup/SPIRV-Cross.git "%SOURCE_DIR%"
    if errorlevel 1 exit /b 1
) else if exist "%SOURCE_DIR%\.git" (
    git -C "%SOURCE_DIR%" fetch --depth 1 origin "refs/tags/%TAG%:refs/tags/%TAG%"
    if errorlevel 1 exit /b 1
    git -C "%SOURCE_DIR%" checkout --force "%TAG%"
    if errorlevel 1 exit /b 1
) else (
    echo SPIRV-Cross Source exists but is not a git checkout: %SOURCE_DIR%
    exit /b 1
)

if "%SKIP_BUILD%"=="1" (
    echo SPIRV-Cross source ready: %SOURCE_DIR%
    exit /b 0
)

cmake -S "%SOURCE_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -T v143 ^
    -DSPIRV_CROSS_CLI=ON ^
    -DSPIRV_CROSS_ENABLE_TESTS=OFF ^
    -DSPIRV_CROSS_ENABLE_GLSL=ON ^
    -DSPIRV_CROSS_ENABLE_MSL=ON ^
    -DSPIRV_CROSS_ENABLE_REFLECT=ON ^
    -DSPIRV_CROSS_ENABLE_UTIL=ON ^
    -DSPIRV_CROSS_SHARED=OFF
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config "%CONFIGURATION%" --target spirv-cross
if errorlevel 1 exit /b 1

set "BUILT_EXE=%BUILD_DIR%\%CONFIGURATION%\spirv-cross.exe"
if not exist "%BUILT_EXE%" (
    echo SPIRV-Cross build completed, but spirv-cross.exe was not found: %BUILT_EXE%
    exit /b 1
)

echo SPIRV-Cross ready: %BUILT_EXE%
exit /b 0
