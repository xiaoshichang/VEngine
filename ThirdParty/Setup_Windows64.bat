@echo off
setlocal EnableExtensions

for %%I in ("%~f0") do set "ROOT=%%~dpI"
for %%I in ("%ROOT%..") do set "REPOSITORY_ROOT=%%~fI"
set "WITH_MSVC=%REPOSITORY_ROOT%\CMake\Scripts\WithMsvc.bat"
set "SKIP_BOOST=0"

:ParseArguments
if "%~1"=="" goto ArgumentsParsed
if /I "%~1"=="-SkipBoost" (
    set "SKIP_BOOST=1"
    shift
    goto ParseArguments
)
if /I "%~1"=="--SkipBoost" (
    set "SKIP_BOOST=1"
    shift
    goto ParseArguments
)

echo Unknown argument: %~1
exit /b 1

:ArgumentsParsed
if "%SKIP_BOOST%"=="1" goto SkipBoostSetup

call :TestBoostReady
if "%BOOST_READY%"=="1" (
    echo Boost already prepared: %ROOT%Boost\Build\Windows64
) else (
    pushd "%ROOT%Boost" || exit /b 1
    python -m pip install wget
    if errorlevel 1 (
        popd
        exit /b 1
    )

    call "%WITH_MSVC%" python main.py 1.85.0 Windows64
    if errorlevel 1 (
        popd
        exit /b 1
    )
    popd
)

:SkipBoostSetup
call :TestDxcReady || exit /b 1
call "%ROOT%Slang\Setup_Windows64.bat" || exit /b 1
call "%ROOT%DotNet\Setup_Windows64.bat" || exit /b 1
call "%ROOT%WindowsSdkTools\Setup_Windows64.bat" || exit /b 1
call "%ROOT%SPIRV-Cross\Setup_Windows64.bat" || exit /b 1
call "%ROOT%Jolt\Setup_Windows64.bat" || exit /b 1

echo ThirdParty Windows64 setup complete.
exit /b 0

:TestBoostReady
set "BOOST_READY=1"
set "BOOST_LIB_DIR=%ROOT%Boost\Build\Windows64\lib"

if not exist "%BOOST_LIB_DIR%\cmake\Boost-1.85.0\BoostConfig.cmake" set "BOOST_READY=0"
if not exist "%BOOST_LIB_DIR%\libboost_json-vc143-mt-x64-1_85.lib" set "BOOST_READY=0"
if not exist "%BOOST_LIB_DIR%\libboost_json-vc143-mt-gd-x64-1_85.lib" set "BOOST_READY=0"
if not exist "%BOOST_LIB_DIR%\libboost_log-vc143-mt-x64-1_85.lib" set "BOOST_READY=0"
if not exist "%BOOST_LIB_DIR%\libboost_log-vc143-mt-gd-x64-1_85.lib" set "BOOST_READY=0"
if not exist "%BOOST_LIB_DIR%\libboost_log_setup-vc143-mt-x64-1_85.lib" set "BOOST_READY=0"
if not exist "%BOOST_LIB_DIR%\libboost_log_setup-vc143-mt-gd-x64-1_85.lib" set "BOOST_READY=0"
if not exist "%BOOST_LIB_DIR%\libboost_system-vc143-mt-x64-1_85.lib" set "BOOST_READY=0"
if not exist "%BOOST_LIB_DIR%\libboost_system-vc143-mt-gd-x64-1_85.lib" set "BOOST_READY=0"
exit /b 0

:TestDxcReady
set "DXC_READY=1"
set "DXC_DIR=%ROOT%DirectXShaderCompiler"

if not exist "%DXC_DIR%\dxc.exe" set "DXC_READY=0"
if not exist "%DXC_DIR%\dxcompiler.dll" set "DXC_READY=0"
if not exist "%DXC_DIR%\dxil.dll" set "DXC_READY=0"

if "%DXC_READY%"=="1" (
    echo DirectXShaderCompiler ready: %DXC_DIR%\dxc.exe
    exit /b 0
)

echo DirectXShaderCompiler payload is missing under %DXC_DIR%.
exit /b 1
